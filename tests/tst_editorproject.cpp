#include "App/Views/Editor/EditorProject.h"
#include <SocietyDrive.h>
#include "App/Views/Editor/EditorProjectCodec.h"
#include "App/Generation/ImageCanvas.h"
#include <iiFileProvider.h>
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <QImage>
#include <QByteArray>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <array>

class EditorProjectTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage{DREAMSCAPES_TEST_DIRECTORY "/project-documents-XXXXXX.societycontainer"};
    static QImage previewImage(const QUrl &source) {
        const auto value = source.toString();
        if (!value.startsWith("data:image/png;base64,")) return {};
        return QImage::fromData(QByteArray::fromBase64(value.mid(value.indexOf(',') + 1).toLatin1()), "PNG");
    }
    static iiSharedCanvas::Document scatteredPages() {
        using namespace iiSharedCanvas;
        Document document;
        document.extent = {200, 100};
        document.artboards = {{"first", "First", {{-360, 120}, {200, 100}}, 0},
                              {"second", "Second", {{420, -240}, {100, 200}}, 0}};
        const std::array<std::uint32_t, 2> colors{qRgba(240, 40, 30, 123), qRgba(30, 60, 230, 255)};
        for (std::size_t i = 0; i < document.artboards.size(); ++i) {
            const auto &board = document.artboards[i];
            const auto asset = board.id + ".pixels";
            document.assets.emplace_back(RasterAsset{asset, makeRasterLayer(board.region.extent.width,
                board.region.extent.height, colors[i])});
            LayerProperties properties;
            properties.id = board.id + ".layer";
            properties.name = board.name;
            properties.artboardId = board.id;
            document.layers.emplace_back(StaticBitmapLayer{properties, StaticSource{asset}});
        }
        return document;
    }
private slots:
    void initTestCase() {
        QVERIFY(iiSocietyContainer::SocietyDrive::create(m_storage.path()));
        qputenv("SOCIETY_CONTAINER_PATH", m_storage.path().toUtf8());
    }
    void pagePresentationFitsTheActivePageWithoutMovingDocumentGeometry() {
        EditorProject project;
        auto *canvas = project.currentCanvas();
        QVERIFY(!canvas->pagePresentation());
        QVERIFY(canvas->loadProjectPage(scatteredPages(), "Scattered pages"));
        const auto boards = canvas->document()->artboards;
        const auto revision = canvas->revision();
        QQuickItem surface;
        surface.setSize({800, 600});
        project.attachCanvas(&surface);
        canvas->setPagePresentation(true);
        QCOMPARE(canvas->renderArtboardId(), QStringLiteral("first"));
        const auto localPan = [&] {
            return QPointF(canvas->panX(), canvas->panY())
                + (canvas->editingOrigin() - QPointF(canvas->canvasOriginX(), canvas->canvasOriginY())) * canvas->zoom();
        };
        QCOMPARE(canvas->zoom(), 2.8);
        QVERIFY(qAbs(localPan().x() - 120) < 0.001);
        QVERIFY(qAbs(localPan().y() - 160) < 0.001);
        canvas->panBy(10000, -10000);
        QVERIFY(qAbs(localPan().x() - 120) < 0.001);
        QVERIFY(qAbs(localPan().y() - 160) < 0.001);
        QVERIFY(project.setCurrentIndex(1));
        QCOMPARE(project.currentCanvas(), canvas);
        QCOMPARE(canvas->renderArtboardId(), QStringLiteral("second"));
        QCOMPARE(canvas->zoom(), 2.4);
        QVERIFY(qAbs(localPan().x() - 280) < 0.001);
        QVERIFY(qAbs(localPan().y() - 60) < 0.001);
        canvas->setZoom(8);
        canvas->panBy(10000, 10000);
        QVERIFY(qAbs(localPan().x()) < 0.001);
        QVERIFY(qAbs(localPan().y()) < 0.001);
        canvas->panBy(-10000, -10000);
        QVERIFY(qAbs(localPan().x()) < 0.001); // Exactly fitting width stays centered.
        QVERIFY(qAbs(localPan().y() + 1000) < 0.001);
        canvas->fitToView();
        QCOMPARE(canvas->document()->artboards, boards);
        QCOMPARE(canvas->revision(), revision);
        canvas->setPagePresentation(false);
        QVERIFY(!canvas->pagePresentation());
        QVERIFY(canvas->renderArtboardId().isEmpty());
        QCOMPARE(canvas->document()->artboards, boards);
    }
    void pagePresentationPreservesSingleAndInfiniteViewportRules() {
        EditorCanvas canvas;
        QVERIFY(canvas.createCanvas({{"width", 400}, {"height", 200}, {"unit", "px"}, {"background", "White"}}));
        canvas.setSize({800, 600});
        canvas.fitToView();
        const auto zoom = canvas.zoom();
        const QPointF pan{canvas.panX(), canvas.panY()};
        canvas.setPagePresentation(true);
        QCOMPARE(canvas.zoom(), zoom);
        QCOMPARE(QPointF(canvas.panX(), canvas.panY()), pan);
        canvas.panBy(10000, -10000);
        QCOMPARE(QPointF(canvas.panX(), canvas.panY()), pan);
        QVERIFY(canvas.createInfiniteRasterDocument(100, 100));
        canvas.setPanX(-3000); canvas.setPanY(7000);
        QCOMPARE(canvas.panX(), -3000.0); QCOMPARE(canvas.panY(), 7000.0);
    }
    void pageFitUsesTheActualZoomAtSdkBounds() {
        auto document = scatteredPages();
        document.artboards[1].region = {{80000, -90000}, {65536, 65536}};
        EditorProject project;
        auto *canvas = project.currentCanvas();
        QVERIFY(canvas->loadProjectPage(std::move(document), "Large page"));
        canvas->setSize({320, 240});
        canvas->setPagePresentation(true);
        QVERIFY(project.setCurrentIndex(1));
        QCOMPARE(canvas->zoom(), 0.01);
        const auto center = QPointF(canvas->panX(), canvas->panY())
            + (canvas->editingOrigin() - QPointF(canvas->canvasOriginX(), canvas->canvasOriginY())
                + QPointF(canvas->editingWidth() / 2.0, canvas->editingHeight() / 2.0)) * canvas->zoom();
        QVERIFY(QLineF(center, QPointF(160, 120)).length() < 0.001);
    }
    void replacingFocusedPagesWithASingleCanvasClearsRenderFocus() {
        EditorProject project;
        auto *canvas = project.currentCanvas();
        QVERIFY(canvas->loadProjectPage(scatteredPages(), "Focused pages"));
        canvas->setSize({800, 600});
        canvas->setPagePresentation(true);
        QVERIFY(project.setCurrentIndex(1));
        QCOMPARE(canvas->renderArtboardId(), QStringLiteral("second"));
        QVERIFY(canvas->createCanvas({{"width", 80}, {"height", 60}, {"unit", "px"}, {"background", "White"}}));
        QVERIFY(canvas->renderArtboardId().isEmpty());
        QCOMPARE(project.canvasCount(), 1);
    }
    void overlappingPageOriginsNeverRedirectNativeInputToAnotherPage() {
        using namespace iiSharedCanvas;
        EditorProject project;
        auto *canvas = project.currentCanvas();
        auto document = scatteredPages();
        document.artboards[1].region.origin = document.artboards[0].region.origin;
        QVERIFY(canvas->loadProjectPage(std::move(document), "Overlapping pages"));
        canvas->setSize({800, 600});
        canvas->setPagePresentation(true);
        canvas->configureTool("brush", {{"field-4", 4}, {"field-5", 100}, {"field-8", 100}, {"field-9", 100}});
        canvas->setBrushColor(Qt::white);
        const auto otherBefore = renderArtboard(*canvas->document(), 0, "second").pixels.pixels;
        const auto clickAt = [&](QPointF local) {
            const auto position = QPointF(canvas->panX(), canvas->panY())
                + (canvas->editingOrigin() - QPointF(canvas->canvasOriginX(), canvas->canvasOriginY()) + local) * canvas->zoom();
            QMouseEvent press(QEvent::MouseButtonPress, position, position, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(canvas, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, position, position, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(canvas, &release);
        };
        clickAt({40, 40}); // This native location belongs to both stored artboards.
        QCOMPARE(project.currentIndex(), 0);
        const auto first = renderArtboard(*canvas->document(), 0, "first");
        QVERIFY(first.ok());
        QVERIFY(first.pixels.pixels[40 * 200 + 40] != qRgba(240, 40, 30, 123));
        QCOMPARE(renderArtboard(*canvas->document(), 0, "second").pixels.pixels, otherBefore);
        const auto revision = canvas->revision();
        clickAt({40, 150}); // Inside the other stored artboard, outside the active page.
        QCOMPARE(project.currentIndex(), 0);
        QCOMPARE(canvas->revision(), revision);
        QCOMPARE(renderArtboard(*canvas->document(), 0, "second").pixels.pixels, otherBefore);
    }
    void pagePreviewsRenderOnlyTheirPageAtTheRequestedAspectAndStayReadOnly() {
        EditorProject project;
        auto *canvas = project.currentCanvas();
        QVERIFY(canvas->loadProjectPage(scatteredPages(), "Scattered pages"));
        const auto boards = canvas->document()->artboards;
        const auto revision = canvas->revision();
        const auto selection = canvas->selectedLayerId();
        const auto firstSource = project.pagePreviewSource(0, 80, 80);
        const auto first = previewImage(firstSource), second = previewImage(project.pagePreviewSource(1, 80, 80));
        QCOMPARE(first.size(), QSize(80, 40));
        QCOMPARE(second.size(), QSize(40, 80));
        QCOMPARE(first.pixel(20, 20), qRgba(240, 40, 30, 123));
        QCOMPARE(second.pixel(20, 20), qRgba(30, 60, 230, 255));
        QCOMPARE(project.pagePreviewSource(0, 80, 80), firstSource);
        QCOMPARE(canvas->document()->artboards, boards);
        QCOMPARE(canvas->revision(), revision);
        QCOMPARE(canvas->selectedLayerId(), selection);
        QCOMPARE(project.currentIndex(), 0);
        QVERIFY(!project.setCurrentIndex(-1));
        const auto error = project.error();
        QVERIFY(project.pagePreviewSource(-1, 80, 80).isEmpty());
        QVERIFY(project.pagePreviewSource(2, 80, 80).isEmpty());
        QCOMPARE(project.error(), error);
        QCOMPARE(previewImage(project.pagePreviewSource(0, 99999, 99999)).size(), QSize(1024, 512));
        QCOMPARE(previewImage(project.pagePreviewSource(1, 0, -10)).size(), QSize(1, 1));
    }
    void pagePreviewCacheTracksEditsUndoFramesAndDocumentReplacement() {
        using namespace iiSharedCanvas;
        EditorProject project;
        auto *canvas = project.currentCanvas();
        QVERIFY(canvas->loadProjectPage(scatteredPages(), "Scattered pages"));
        const auto before = project.pagePreviewSource(0, 100, 100);
        QVERIFY(canvas->setLayerVisible("first.layer", false));
        QCOMPARE(qAlpha(previewImage(project.pagePreviewSource(0, 100, 100)).pixel(20, 20)), 0);
        QVERIFY(canvas->undo());
        QCOMPARE(project.pagePreviewSource(0, 100, 100), before);
        auto animated = scatteredPages();
        animated.timeline.frameCount = 2;
        animated.assets.emplace_back(RasterAsset{"first.next", makeRasterLayer(200, 100, 0xff00ff00u)});
        animated.layers[0] = DynamicBitmapLayer{layerProperties(animated.layers[0]), KeyframedSource{{0, 1}}};
        animated.frames = {{0, {{"first.layer", "first.pixels"}}}, {1, {{"first.layer", "first.next"}}}};
        QVERIFY(canvas->loadProjectPage(std::move(animated), "Animated pages"));
        QCOMPARE(previewImage(project.pagePreviewSource(0, 100, 100)).pixel(20, 20), qRgba(240, 40, 30, 123));
        canvas->setFrame(1);
        QCOMPARE(previewImage(project.pagePreviewSource(0, 100, 100)).pixel(20, 20), 0xff00ff00u);
        canvas->setFrame(0);
        QCOMPARE(previewImage(project.pagePreviewSource(0, 100, 100)).pixel(20, 20), qRgba(240, 40, 30, 123));
        QVERIFY(project.createCanvas({{"width", 40}, {"height", 20}, {"unit", "px"}, {"background", "Black"}}));
        QCOMPARE(previewImage(project.pagePreviewSource(0, 80, 80)).pixel(20, 20), 0xff000000u);
    }
    void pagePreviewNotificationsWaitForStrokeCompletion_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("finish") << false;
        QTest::newRow("cancel") << true;
    }
    void pagePreviewNotificationsWaitForStrokeCompletion() {
        QFETCH(bool, cancel);
        EditorProject project;
        auto *canvas = project.currentCanvas();
        QVERIFY(canvas->loadProjectPage(scatteredPages(), "Stroke previews"));
        canvas->configureTool("brush", {{"field-4", 8}, {"field-5", 100}, {"field-8", 100}, {"field-9", 100}});
        canvas->setBrushColor(Qt::white);
        const auto before = project.pagePreviewSource(0, 200, 100);
        const auto beforeImage = previewImage(before);
        QVERIFY(!beforeImage.isNull());
        const auto epoch = project.pagePreviewRevision();
        QSignalSpy changed(&project, &EditorProject::pagePreviewsChanged);
        QVERIFY(changed.isValid());
        QVERIFY(canvas->beginStrokeAt({40, 40}));
        QVERIFY(canvas->liveStrokeActive());
        QCOMPARE(changed.count(), 0);
        QCOMPARE(project.pagePreviewRevision(), epoch);
        QVERIFY(project.pagePreviewSource(0, 200, 100) != before);
        for (int x : {45, 50, 55}) {
            QVERIFY(canvas->continueStrokeAt({qreal(x), 40}));
            QCOMPARE(changed.count(), 0);
            QCOMPARE(project.pagePreviewRevision(), epoch);
            // Direct API reads follow the native revision even while the UI
            // waits for the stroke to finish.
            const auto latest = previewImage(project.pagePreviewSource(0, 200, 100));
            QVERIFY(!latest.isNull());
            QVERIFY(latest.pixel(x, 40) != beforeImage.pixel(x, 40));
        }
        if (cancel) canvas->cancelStroke();
        else QVERIFY(canvas->endStrokeAt({60, 40}));
        QVERIFY(!canvas->liveStrokeActive());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(project.pagePreviewRevision(), epoch + 1);
        const auto after = project.pagePreviewSource(0, 200, 100);
        if (cancel) QCOMPARE(after, before);
        else QVERIFY(after != before);
    }
    void pagePreviewNotificationsCoalesceMatchingNativeState() {
        EditorProject project;
        auto *canvas = project.currentCanvas();
        QVERIFY(canvas->loadProjectPage(scatteredPages(), "Preview state"));
        const auto before = project.pagePreviewSource(0, 100, 100);
        QSignalSpy changed(&project, &EditorProject::pagePreviewsChanged);
        QVERIFY(changed.isValid());
        QVERIFY(canvas->setLayerVisible("first.layer", false));
        QCOMPARE(changed.count(), 1); // revisionChanged and documentChanged.
        QCOMPARE(qAlpha(previewImage(project.pagePreviewSource(0, 100, 100)).pixel(20, 20)), 0);
        QVERIFY(canvas->undo());
        QCOMPARE(changed.count(), 2);
        QCOMPARE(project.pagePreviewSource(0, 100, 100), before);
        QVERIFY(canvas->redo());
        QCOMPARE(changed.count(), 3);
        auto animated = scatteredPages();
        animated.timeline.frameCount = 2;
        QVERIFY(canvas->loadProjectPage(std::move(animated), "Preview frames"));
        changed.clear();
        canvas->setFrame(1);
        QCOMPARE(changed.count(), 1); // revisionChanged and frameChanged.
        canvas->setFrame(1);
        QCOMPARE(changed.count(), 1);
        canvas->setFrame(0);
        QCOMPARE(changed.count(), 2);
        const auto latest = project.pagePreviewSource(0, 100, 100);
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/preview-state-XXXXXX");
        QVERIFY(directory.isValid());
        changed.clear();
        QVERIFY(!project.openDocumentSource(QUrl::fromLocalFile(directory.filePath("missing.iisc"))));
        QCOMPARE(project.currentCanvas(), canvas);
        QCOMPARE(changed.count(), 0);
        const auto error = project.error();
        QVERIFY(!error.isEmpty());
        QCOMPARE(project.pagePreviewSource(0, 100, 100), latest);
        QCOMPARE(project.error(), error);
        canvas->unbind();
        QCOMPARE(changed.count(), 1); // documentChanged, frameChanged and revisionChanged.
        QVERIFY(project.pagePreviewSource(0, 100, 100).isEmpty());
        changed.clear();
        QVERIFY(canvas->loadProjectPage(scatteredPages(), "Rebound previews"));
        QCOMPARE(project.currentCanvas(), canvas);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(project.pagePreviewSource(0, 100, 100), before);
    }
    void replacingCanvasResetsPreviewStateEvenWhenNativeCountersMatch() {
        EditorProject project;
        QVERIFY(project.createCanvas({{"width", 40}, {"height", 20}, {"unit", "px"}, {"background", "White"}}));
        auto *before = project.currentCanvas();
        const auto revision = before->revision();
        const auto frame = before->frame();
        QCOMPARE(previewImage(project.pagePreviewSource(0, 80, 80)).pixel(20, 20), 0xffffffffu);
        QSignalSpy changed(&project, &EditorProject::pagePreviewsChanged);
        QVERIFY(changed.isValid());
        QVERIFY(project.createCanvas({{"width", 40}, {"height", 20}, {"unit", "px"}, {"background", "Black"}}));
        QVERIFY(project.currentCanvas() != before);
        QCOMPARE(project.currentCanvas()->revision(), revision);
        QCOMPARE(project.currentCanvas()->frame(), frame);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(previewImage(project.pagePreviewSource(0, 80, 80)).pixel(20, 20), 0xff000000u);
    }
    void attachedImagesShareOneNativeArtboardDocument() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/artboards-XXXXXX");
        QVERIFY(directory.isValid());
        QVariantList sources;
        for (const auto size : {QSize(40, 24), QSize(20, 48)}) {
            QImage image(size, QImage::Format_ARGB32); image.fill(QColor(30, 80, 170, 123));
            const auto path = directory.filePath(QString::number(sources.size()) + ".png");
            QVERIFY(image.save(path)); sources.append(QUrl::fromLocalFile(path));
        }
        sources[1] = QUrl::fromLocalFile(dreamscapes::persistImageCanvas(sources[1].toUrl().toLocalFile(), nullptr));
        EditorProject project;
        QVERIFY(project.openImages(sources));
        const auto *document = project.currentCanvas()->document();
        QCOMPARE(document->artboards.size(), 2u);
        QCOMPARE(document->layers.size(), 2u);
        const auto first = document->artboards[0], second = document->artboards[1];
        QCOMPARE(first.region.extent, (iiSharedCanvas::CanvasExtent{40, 24}));
        QCOMPARE(second.region.extent, (iiSharedCanvas::CanvasExtent{20, 48}));
        QVERIFY(second.region.origin.x > first.region.origin.x + first.region.extent.width);
        QCOMPARE(first.region.origin.y, second.region.origin.y);
        for (const auto &board : document->artboards) {
            const auto rendered = iiSharedCanvas::renderArtboard(*document, 0, board.id);
            QVERIFY(rendered.ok()); QCOMPARE(rendered.pixels.pixels.front(), qRgba(30, 80, 170, 123));
        }
        const auto overview = iiSharedCanvas::renderFrame(*document, 0);
        QVERIFY(overview.ok());
        QCOMPARE(overview.pixels.pixels[40],0u); // Transparent gap after the first board.
        auto *canvas = project.currentCanvas();
        QVERIFY(project.setCurrentIndex(1)); QCOMPARE(project.currentCanvas(), canvas);
        canvas->configureTool("elements",{{"selector","Rectangle"},{"field-16","#FF0000"},{"field-2","Solid"}});
        QVERIFY(canvas->createElement({2,3,6,7})); QVERIFY(canvas->rasterizeSelected());
        QCOMPARE(canvas->selectedRasterPixels()->width,20);
        QCOMPARE(canvas->selectedRasterPixels()->height,48);
        QCOMPARE(iiSharedCanvas::renderArtboard(*canvas->document(),0,first.id).pixels.pixels.front(),qRgba(30,80,170,123));
        canvas->configureTool("background",{{"field-0","Solid"},{"field-1","#000000"},{"field-2",100}});
        QVERIFY(canvas->applyToolField("background","field-1","#000000"));
        QVERIFY(project.setCurrentIndex(0));
        canvas->configureTool("background",{{"field-0","Solid"},{"field-1","#FFFFFF"},{"field-2",100}});
        QVERIFY(canvas->applyToolField("background","field-1","#FFFFFF"));
        QVERIFY(project.setCurrentIndex(1));
        const auto exported=directory.filePath("second.png"); QVERIFY(canvas->exportImage(QUrl::fromLocalFile(exported),"PNG"));
        QCOMPARE(QImage(exported).size(),QSize(20,48));
        const auto destination = directory.filePath("all.iisc");
        QVERIFY(project.saveDocumentAs(QUrl::fromLocalFile(destination)));
        EditorProject reopened; QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(destination)));
        QCOMPARE(reopened.currentCanvas()->document()->artboards.size(), 2u);
    }
    void mountUsesFullDocumentAndPhysicalViewport() {
        QQuickWindow window;
        QQuickItem surface(window.contentItem());
        surface.setSize({480, 360});
        EditorProject project;
        QVERIFY(project.createCanvas({{"width", 1024}, {"height", 768}, {"unit", "px"}, {"background", "White"}}));
        auto *canvas = project.currentCanvas();
        canvas->setSmoothRendering(false);
        const auto revision = canvas->revision();
        project.attachCanvas(&surface);
        QVERIFY(canvas->smoothRendering());
        QCOMPARE(canvas->parentItem(), &surface);
        QCOMPARE(canvas->size(), surface.size());
        QCOMPARE(canvas->renderDevicePixelRatio(), window.effectiveDevicePixelRatio());
        QCOMPARE(canvas->revision(), revision);
        canvas->setZoom(0.45); canvas->setPanX(13); canvas->setPanY(17);
        project.attachCanvas(&surface);
        QCOMPARE(canvas->zoom(), 0.45);
        QCOMPARE(canvas->panX(), (480 - 1024 * 0.45) / 2);
        QCOMPARE(canvas->panY(), (360 - 768 * 0.45) / 2);
        project.attachCanvas(nullptr);
        QCOMPARE(canvas->parentItem(), &surface);
    }
    void independentCanvasesEditNavigateAndReopen() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/editor-project-XXXXXX");
        QVERIFY(directory.isValid());
        QVariantList sources;
        const QList<QSize> sizes{{40,24},{20,48},{16,16}};
        const QList<QColor> colors{Qt::red,Qt::blue,Qt::green};
        for (int i=0;i<3;++i) {
            QImage image(sizes[i],QImage::Format_ARGB32); image.fill(colors[i]);
            const auto path=directory.filePath(QString("image %1 %% #.png").arg(i));
            QVERIFY(image.save(path)); sources.append(QUrl::fromLocalFile(path));
        }
        EditorProject project;
        QVERIFY(project.openImages(sources+QVariantList{sources[0]}));
        QCOMPARE(project.canvasCount(),3); QCOMPARE(project.currentIndex(),0);
        auto *canvas=project.currentCanvas();
        QCOMPARE(canvas->editingWidth(),40); QCOMPARE(canvas->editingHeight(),24);
        QCOMPARE(canvas->layers().size(),1); QCOMPARE(canvas->document()->layers.size(),3u);
        const auto firstId=canvas->document()->artboards[0].id;
        const auto secondId=canvas->document()->artboards[1].id;
        const auto greenId=canvas->document()->artboards[2].id;
        QVERIFY(project.setCurrentIndex(1)); QCOMPARE(project.currentCanvas(),canvas);
        QCOMPARE(canvas->editingWidth(),20); QCOMPARE(canvas->editingHeight(),48);
        canvas->configureTool("brush",{{"field-4",6},{"field-5",100},{"field-8",100},{"field-9",100}});
        canvas->setBrushColor(Qt::yellow);
        QVERIFY(canvas->beginStrokeAt({10,20})); QVERIFY(!project.setCurrentIndex(0));
        QVERIFY(canvas->endStrokeAt({10,20})); QVERIFY(canvas->canUndo());
        const auto edited=iiSharedCanvas::renderArtboard(*canvas->document(),0,secondId).pixels.pixels;
        QVERIFY(edited[20*20+10]!=QColor(Qt::blue).rgba());
        QVERIFY(canvas->undo()); QCOMPARE(iiSharedCanvas::renderArtboard(*canvas->document(),0,secondId).pixels.pixels[410],QColor(Qt::blue).rgba());
        QVERIFY(canvas->redo());
        QVERIFY(canvas->addPaintLayer());
        const auto *paint=iiSharedCanvas::findLayer(*canvas->document(),canvas->selectedLayerId().toStdString());
        QCOMPARE(iiSharedCanvas::layerProperties(*paint).artboardId,std::optional<std::string>(secondId));
        QCOMPARE(canvas->selectedRasterPixels()->width,20); QCOMPARE(canvas->selectedRasterPixels()->height,48);
        QVERIFY(canvas->placeImage(sources[2].toUrl(),"1:1"));
        QCOMPARE(iiSharedCanvas::layerProperties(*iiSharedCanvas::findLayer(*canvas->document(),canvas->selectedLayerId().toStdString())).artboardId,std::optional<std::string>(secondId));
        QVERIFY(canvas->undo()); QVERIFY(canvas->undo());
        QVERIFY(project.setCurrentIndex(0));
        QCOMPARE(iiSharedCanvas::renderArtboard(*canvas->document(),0,firstId).pixels.pixels.front(),QColor(Qt::red).rgba());
        QVERIFY(project.setCurrentIndex(2));
        QCOMPARE(iiSharedCanvas::renderArtboard(*canvas->document(),0,greenId).pixels.pixels.front(),QColor(Qt::green).rgba());
        QVERIFY(!project.setCurrentIndex(-1)); QVERIFY(!project.setCurrentIndex(3));
        const auto path=directory.filePath("project %20 #.iisc");
        QVERIFY(project.saveDocumentAs(QUrl::fromLocalFile(path))); QVERIFY(!project.modified());
        QVERIFY(!project.openImages({sources[0],QUrl::fromLocalFile(directory.filePath("missing.png"))}));
        QCOMPARE(project.canvasCount(),3); QCOMPARE(project.currentCanvas(),canvas); QCOMPARE(project.filePath(),path);
        for (const auto &source : sources) QVERIFY(QFile::remove(source.toUrl().toLocalFile()));
        EditorProject reopened; QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(path)));
        QCOMPARE(reopened.canvasCount(),3); QCOMPARE(reopened.currentIndex(),0);
        QCOMPARE(iiSharedCanvas::renderArtboard(*reopened.currentCanvas()->document(),0,secondId).pixels.pixels,edited);
        QVERIFY(reopened.setCurrentIndex(1));
        const auto layer=reopened.currentCanvas()->selectedLayerId();
        QVERIFY(reopened.currentCanvas()->setLayerName(layer,"Renamed"));
        EditorProject latest; QVERIFY(latest.openDocumentSource(QUrl::fromLocalFile(path)));
        QCOMPARE(iiSharedCanvas::layerProperties(*iiSharedCanvas::findLayer(*latest.currentCanvas()->document(),layer.toStdString())).name,std::string("Renamed"));
        QVERIFY(latest.setCurrentIndex(1));
        QVERIFY(latest.currentCanvas()->applyToolField("canvas","field-0",QVariantList{28,52}));
        QCOMPARE(latest.currentCanvas()->document()->artboards[1].region.extent,(iiSharedCanvas::CanvasExtent{28,52}));
        QCOMPARE(iiSharedCanvas::renderArtboard(*latest.currentCanvas()->document(),0,firstId).pixels.pixels.front(),QColor(Qt::red).rgba());
        QCOMPARE(iiSharedCanvas::renderArtboard(*latest.currentCanvas()->document(),0,greenId).pixels.pixels.front(),QColor(Qt::green).rgba());
    }
    void attachmentsExtendTheCurrentWorkingFileAtomically() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/attach-artboards-XXXXXX");
        EditorProject project;
        QVERIFY(project.createCanvas({{"width",32},{"height",24},{"unit","px"},{"background","White"}}));
        const auto destination=directory.filePath("existing.iisc");
        QVERIFY(project.saveDocumentAs(QUrl::fromLocalFile(destination)));
        QVariantList sources;
        for (int i=0;i<2;++i) {
            QImage image(16+i*4,20,QImage::Format_ARGB32); image.fill(i ? Qt::green : Qt::red);
            const auto path=directory.filePath(QString::number(i)+".png"); QVERIFY(image.save(path)); sources.append(QUrl::fromLocalFile(path));
        }
        auto *canvas=project.currentCanvas();
        QVERIFY(!project.attachImages({sources[0],QUrl::fromLocalFile(directory.filePath("missing.png"))}));
        QCOMPARE(project.canvasCount(),1); QVERIFY(canvas->document()->artboards.empty());
        QVERIFY(project.attachImages(sources)); QCOMPARE(project.currentCanvas(),canvas);
        QCOMPARE(project.canvasCount(),3); QCOMPARE(project.currentIndex(),1); QCOMPARE(project.filePath(),destination);
        const auto document=*canvas->document();
        QCOMPARE(iiSharedCanvas::renderArtboard(document,0,document.artboards[0].id).pixels.pixels.front(),0xffffffffu);
        QCOMPARE(iiSharedCanvas::renderArtboard(document,0,document.artboards[1].id).pixels.pixels.front(),0xffff0000u);
        EditorProject observer; QVERIFY(observer.openDocumentSource(QUrl::fromLocalFile(destination))); QCOMPARE(observer.canvasCount(),3);
        QVERIFY(canvas->undo()); QCOMPARE(project.canvasCount(),1); QVERIFY(canvas->document()->artboards.empty());
        QVERIFY(canvas->redo()); QCOMPARE(project.canvasCount(),3);
        QVERIFY(project.attachImages({sources[0]})); QCOMPARE(project.canvasCount(),4);
        QVERIFY(!project.attachImages({QUrl::fromLocalFile(directory.filePath("missing.png"))})); QCOMPARE(project.canvasCount(),4);
    }
    void legacyProjectMigratesLayersWithoutOverwritingOriginal() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/legacy-artboards-XXXXXX");
        EditorCanvas first,second;
        QVERIFY(first.createCanvas({{"width",32},{"height",24},{"unit","px"},{"background","White"}}));
        QVERIFY(second.createCanvas({{"width",16},{"height",40},{"unit","px"},{"background","Black"}}));
        QVERIFY(second.addPaintLayer());
        auto animated = *second.document();
        animated.timeline.frameCount = 2;
        animated.assets.emplace_back(iiSharedCanvas::RasterAsset{"dynamic.red",makeRasterLayer(2,2,0xffff0000)});
        animated.assets.emplace_back(iiSharedCanvas::RasterAsset{"dynamic.green",makeRasterLayer(2,2,0xff00ff00)});
        animated.layers.emplace_back(iiSharedCanvas::DynamicBitmapLayer{{"dynamic.layer","Animated fill"},iiSharedCanvas::KeyframedSource{{0,1}}});
        animated.frames = {{0,{{"dynamic.layer","dynamic.red"}}},{1,{{"dynamic.layer","dynamic.green"}}}};
        for(auto &layer:animated.layers)iiSharedCanvas::layerProperties(layer).frameRange=iiSharedCanvas::LayerFrameRange{0,1};
        const std::vector<dreamscapes::ProjectPageView> pages{{"First",first.document()},{"Second",&animated}};
        const auto encoded=dreamscapes::encodeProject(pages,1); QVERIFY(encoded.error.empty());
        const auto path=directory.filePath("legacy.iiscp");
        const QByteArray bytes(reinterpret_cast<const char *>(encoded.bytes.data()),encoded.bytes.size());
        iiFileProvider::File::write(path,bytes);
        EditorProject project; QVERIFY(project.openDocumentSource(QUrl::fromLocalFile(path)));
        QCOMPARE(project.canvasCount(),2); QCOMPARE(project.currentIndex(),1); QVERIFY(project.filePath().isEmpty());
        QCOMPARE(project.currentCanvas()->document()->layers.size(),4u);
        const auto &document=*project.currentCanvas()->document();
        QCOMPARE(document.timeline.frameCount,24u);
        const auto *dynamic=iiSharedCanvas::findLayer(document,"attachment.1:dynamic.layer");QVERIFY(dynamic);
        QCOMPARE(iiSharedCanvas::layerProperties(*dynamic).frameRange->lastFrame,1u);
        QCOMPARE(iiSharedCanvas::renderArtboard(document,0,document.artboards[1].id).pixels.pixels.front(),0xffff0000u);
        QCOMPARE(iiSharedCanvas::renderArtboard(document,1,document.artboards[1].id).pixels.pixels.front(),0xff00ff00u);
        QCOMPARE(iiFileProvider::File::read(path),bytes);
        QVERIFY(!project.saveDocumentAs(QUrl::fromLocalFile(path))); QCOMPARE(iiFileProvider::File::read(path),bytes);
        QVERIFY(project.saveDocumentAs(QUrl::fromLocalFile(directory.filePath("migrated.iisc"))));
        auto corrupted=bytes; corrupted.chop(1); iiFileProvider::File::write(directory.filePath("broken.iiscp"),corrupted);
        auto *before=project.currentCanvas();
        QVERIFY(!project.openDocumentSource(QUrl::fromLocalFile(directory.filePath("broken.iiscp"))));
        QCOMPARE(project.currentCanvas(),before); QCOMPARE(project.canvasCount(),2);
    }
    void singleNativeDocumentCompatibility() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/single-project-XXXXXX");
        EditorProject project;
        QVERIFY(project.createCanvas({{"width",32},{"height",24},{"unit","px"},{"background","White"}}));
        QVERIFY(!project.isProject());
        const auto path=directory.filePath("single.iisc");
        QVERIFY(project.saveDocumentAs(QUrl::fromLocalFile(path)));
        QCOMPARE(project.filePath(),path);
        EditorProject reopened;
        QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(path)));
        QCOMPARE(reopened.canvasCount(),1); QCOMPARE(reopened.currentCanvas()->canvasHeight(),24);
    }
};
QTEST_MAIN(EditorProjectTests)
#include "tst_editorproject.moc"
