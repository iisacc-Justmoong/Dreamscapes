#include "App/Views/Home/CanvasPreview.h"
#include <iiSharedCanvas.h>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class CanvasPreviewTests : public QObject {
    Q_OBJECT
private:
    static iiSharedCanvas::Document artwork() {
        iiSharedCanvas::Document document;
        document.extent = {80, 40};
        document.assets.emplace_back(iiSharedCanvas::RasterAsset{"paint", makeRasterLayer(80, 40, 0x80ff0000U)});
        document.layers.emplace_back(iiSharedCanvas::StaticBitmapLayer{{"layer", "Layer"}, iiSharedCanvas::StaticSource{"paint"}});
        return document;
    }
    static QVariantMap entry(const QString &path) {
        return {{"path", path}, {"name", QFileInfo(path).fileName()}, {"available", true}};
    }
private slots:
    void initTestCase() { qmlRegisterType<CanvasPreview>("Dreamscapes.Storage", 1, 0, "CanvasPreview"); }
    void ordinaryAndUnavailableFilesKeepTheirPreviews() {
        const QUrl png("file:///unchanged.png?v=5");
        QCOMPARE(dreamscapes::canvasPreviewSource({{"path", "/file.png"}, {"previewSource", png}}), png);
        QCOMPARE(dreamscapes::canvasPreviewSource({{"path", "/file.iisc"}, {"available", false}, {"previewSource", png}}), png);
        QCOMPARE(dreamscapes::canvasPreviewSource({{"source", QUrl("https://example.invalid/file.iisc")}, {"previewSource", png}}), png);
        QVERIFY(dreamscapes::canvasPreviewSource({}).isEmpty());
    }
    void modulePixelsAreBoundedAndRevisioned() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/canvas-preview-XXXXXX");
        const auto path = root.filePath("image %20 # 한글.IISC");
        iiSharedCanvas::DocumentFile owner;
        QVERIFY(owner.create(path.toStdString(), artwork()).ok());
        QQmlEngine engine; dreamscapes::installCanvasPreviewProvider(engine);
        auto *provider = dynamic_cast<QQuickImageProvider *>(engine.imageProvider("iisc-preview"));
        QVERIFY(provider);
        QVERIFY(provider->flags().testFlag(QQuickImageProvider::ForceAsynchronousImageLoading));
        const auto source = dreamscapes::canvasPreviewSource(entry(path));
        QCOMPARE(source.scheme(), "image"); QCOMPARE(source.host(), "iisc-preview");
        QSize size;
        const auto image = provider->requestImage(source.path().mid(1) + '?' + source.query(), &size, {20, 20});
        QCOMPARE(size, QSize(20, 10)); QCOMPARE(image.size(), size);
        QCOMPARE(image.pixel(10, 5), 0x80ff0000U);
        QVERIFY(owner.edit([](iiSharedCanvas::Document &document) {
            std::get<iiSharedCanvas::RasterAsset>(document.assets.front()).pixels.pixels.assign(3200, 0xff0000ffU);
            return true;
        }).ok());
        const auto updated = dreamscapes::canvasPreviewSource(entry(path));
        QVERIFY2(updated != source, "same-size live WAL edits must produce a fresh image cache URL");
        const auto blue = provider->requestImage(updated.path().mid(1) + '?' + updated.query(), &size, {20, 20});
        QCOMPARE(blue.pixel(10, 5), 0xff0000ffU);
        QVERIFY(provider->requestImage("not-base64", &size, {}).isNull()); QVERIFY(size.isEmpty());
    }
    void qmlLoadsSnapshotAndWorkingFileAsynchronously_data() {
        QTest::addColumn<bool>("working");
        QTest::newRow("legacy-snapshot") << false;
        QTest::newRow("live-working-file") << true;
    }
    void qmlLoadsSnapshotAndWorkingFileAsynchronously() {
        QFETCH(bool, working);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/canvas-preview-qml-XXXXXX");
        const auto path = root.filePath("document.iisc");
        iiSharedCanvas::DocumentFile owner;
        if (working) QVERIFY(owner.create(path.toStdString(), artwork()).ok());
        else {
            const auto encoded = iiSharedCanvas::encodeIisc(artwork()); QVERIFY(encoded.ok());
            QFile snapshot(path); QVERIFY(snapshot.open(QIODevice::WriteOnly));
            snapshot.write(reinterpret_cast<const char *>(encoded.bytes.data()), qint64(encoded.bytes.size()));
        }
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("fileEntry", entry(path));
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Dreamscapes.Storage 1.0
            Item {
                CanvasPreview { id: previews }
                Image {
                    objectName: "preview"
                    source: previews.sourceForFile(fileEntry)
                    sourceSize: Qt.size(20, 20)
                }
            })", QUrl{});
        std::unique_ptr<QObject> view(component.create());
        QVERIFY2(view, qPrintable(component.errorString()));
        auto *image = view->findChild<QObject *>("preview"); QVERIFY(image);
        QTRY_COMPARE_WITH_TIMEOUT(image->property("status").toInt(), 1, 10000);
        QCOMPARE(image->property("implicitWidth").toDouble(), 20.0);
        QCOMPARE(image->property("implicitHeight").toDouble(), 10.0);
        QVERIFY(engine.imageProvider("iisc-preview"));
        owner.close(); QVERIFY(QFile::remove(path));
        engine.rootContext()->setContextProperty("fileEntry", entry(root.filePath("missing.iisc")));
        QTRY_COMPARE(image->property("status").toInt(), 3);
    }
};
QTEST_MAIN(CanvasPreviewTests)
#include "tst_canvaspreview.moc"
