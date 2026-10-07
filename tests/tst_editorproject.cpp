#include "App/Views/Editor/EditorProject.h"
#include "App/Views/Editor/EditorProjectCodec.h"
#include <iiFileProvider.h>
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <QImage>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

class EditorProjectTests : public QObject {
    Q_OBJECT
private slots:
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
        auto *first=project.currentCanvas();
        QCOMPARE(first->canvasWidth(),40); QCOMPARE(first->canvasHeight(),24);
        QCOMPARE(first->document()->layers.size(),1u);
        QVERIFY(project.setCurrentIndex(1));
        auto *second=project.currentCanvas();
        QCOMPARE(second->canvasWidth(),20); QCOMPARE(second->canvasHeight(),48);
        second->configureTool("brush",{{"field-4",6},{"field-5",100},{"field-8",100},{"field-9",100}});
        second->setBrushColor(Qt::yellow);
        QVERIFY(second->beginStrokeAt({10,20})); QVERIFY(second->endStrokeAt({10,20}));
        QVERIFY(second->canUndo());
        const auto edited=iiSharedCanvas::renderFrame(*second->document(),0).pixels.pixels;
        QVERIFY(edited[20*20+10]!=QColor(Qt::blue).rgba());
        second->setZoom(3); second->setPanX(11); second->setPanY(7);
        QVERIFY(project.setCurrentIndex(0)); QCOMPARE(project.currentCanvas(),first);
        QCOMPARE(iiSharedCanvas::renderFrame(*first->document(),0).pixels.pixels.front(),QColor(Qt::red).rgba());
        QVERIFY(project.setCurrentIndex(1)); QCOMPARE(project.currentCanvas(),second);
        QCOMPARE(second->zoom(),3.0); QCOMPARE(second->panX(),11.0);
        QVERIFY(second->undo()); QVERIFY(second->redo());
        QCOMPARE(iiSharedCanvas::renderFrame(*second->document(),0).pixels.pixels,edited);
        QVERIFY(!project.setCurrentIndex(-1)); QVERIFY(!project.setCurrentIndex(3));
        const auto path=directory.filePath("project %20 #.iiscp");
        QVERIFY(project.saveDocumentAs(QUrl::fromLocalFile(path))); QVERIFY(!project.modified());
        const auto saved=iiFileProvider::File::read(path);
        QVERIFY(!project.saveDocumentAs(QUrl::fromLocalFile(directory.filePath("wrong.iisc"))));
        QVERIFY(!project.openImages({sources[0],QUrl::fromLocalFile(directory.filePath("missing.png"))}));
        QCOMPARE(project.canvasCount(),3); QCOMPARE(project.currentCanvas(),second);
        QCOMPARE(project.filePath(),path); QCOMPARE(iiFileProvider::File::read(path),saved);
        EditorProject reopened;
        QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(path)));
        QCOMPARE(reopened.canvasCount(),3); QCOMPARE(reopened.currentIndex(),1);
        QCOMPARE(iiSharedCanvas::renderFrame(*reopened.currentCanvas()->document(),0).pixels.pixels,edited);
        QVERIFY(reopened.setCurrentIndex(2));
        QCOMPARE(reopened.currentCanvas()->canvasWidth(),16);
        QCOMPARE(iiSharedCanvas::renderFrame(*reopened.currentCanvas()->document(),0).pixels.pixels.front(),QColor(Qt::green).rgba());
        QVERIFY(reopened.currentCanvas()->setLayerName(reopened.currentCanvas()->selectedLayerId(),"Renamed"));
        QVERIFY(reopened.modified()); QVERIFY(reopened.saveDocument());
        EditorProject latest; QVERIFY(latest.openDocumentSource(QUrl::fromLocalFile(path)));
        QCOMPARE(latest.currentIndex(),2); QCOMPARE(latest.currentCanvas()->selectedLayer().value("name").toString(),"Renamed");
        QFile broken(directory.filePath("broken.iiscp")); QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write(saved.left(saved.size()-1)); broken.close();
        auto *before=latest.currentCanvas();
        QVERIFY(!latest.openDocumentSource(QUrl::fromLocalFile(broken.fileName())));
        QCOMPARE(latest.currentCanvas(),before); QCOMPARE(latest.filePath(),path);
        auto bytes=std::vector<std::uint8_t>(saved.begin(),saved.end());
        bytes.push_back(0); QVERIFY(!dreamscapes::decodeProject(bytes).error.empty());
        bytes.resize(23); QVERIFY(!dreamscapes::decodeProject(bytes).error.empty());
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
