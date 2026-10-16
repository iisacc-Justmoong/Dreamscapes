#include "App/Generation/ImageCanvas.h"
#include "App/Views/Editor/EditorProject.h"
#include "App/Views/Home/HomeCanvas.h"
#include <SocietyDrive.h>
#include <iiSocietyContainer/DashboardFiles.h>
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <iiSharedCanvas/Serialization/IiscCodec.h>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QProcess>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class ImageCanvasTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage{DREAMSCAPES_TEST_DIRECTORY "/image-documents-XXXXXX.societycontainer"};
private slots:
    void initTestCase() {
        QVERIFY(iiSocietyContainer::SocietyDrive::create(m_storage.path()));
        qputenv("SOCIETY_CONTAINER_PATH", m_storage.path().toUtf8());
    }
    void imageDocumentsBelongToFilesAndPngHistoryIsPreserved() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/files-documents-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        const auto source = root.filePath("Generation History/generated.png");
        QImage png(17, 31, QImage::Format_ARGB32); png.fill(QColor(20, 40, 80, 128)); QVERIFY(png.save(source));
        QFile original(source); QVERIFY(original.open(QIODevice::ReadOnly)); const auto bytes = original.readAll(); original.close();
        QString error; const auto native = dreamscapes::persistImageCanvas(source, &error);
        QVERIFY2(native.startsWith(root.filePath("Files/")), qPrintable(error));
        QCOMPARE(dreamscapes::persistImageCanvas(source, &error), native);
        QVERIFY(QDir(root.filePath("Generation History")).entryList({"*.iisc"}, QDir::Files).isEmpty());
        iiSocietyContainer::DashboardFiles dashboard; dashboard.setContainerPath(root.path()); QTRY_VERIFY(!dashboard.loading());
        QCOMPARE(dashboard.generationHistory().size(), 1); QCOMPARE(dashboard.recentFiles().size(), 1);
        QCOMPARE(dashboard.recentFiles().first().toMap().value("path").toString(), native);
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), bytes); original.close();
        EditorProject editor; QVERIFY(editor.openImages({QUrl::fromLocalFile(source)}));
        QCOMPARE(editor.currentCanvas()->canvasWidth(), 17); QCOMPARE(editor.currentCanvas()->canvasHeight(), 31);
        QVERIFY(QDir(root.filePath("Generation History")).entryList({"*.iisc"}, QDir::Files).isEmpty());
    }
    void explicitDocumentStorageDoesNotFollowAnotherSourceDrive() {
        QTemporaryDir sourceDrive(DREAMSCAPES_TEST_DIRECTORY "/source-drive-XXXXXX.societycontainer");
        QTemporaryDir selectedDrive(DREAMSCAPES_TEST_DIRECTORY "/selected-drive-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(sourceDrive.path()));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(selectedDrive.path()));
        const auto source = sourceDrive.filePath("Generation History/reference.png");
        QImage png(19, 11, QImage::Format_ARGB32); png.fill(Qt::red); QVERIFY(png.save(source));
        QString error;
        const auto native = dreamscapes::persistImageCanvas(source, &error, selectedDrive.path());
        QVERIFY2(native.startsWith(selectedDrive.filePath("Files/")), qPrintable(error));
        QVERIFY(QDir(sourceDrive.filePath("Files")).entryList({"*.iisc"}, QDir::Files).isEmpty());
        QVERIFY(dreamscapes::persistImageCanvas(source, &error, sourceDrive.filePath("missing")).isEmpty());
        QVERIFY(QDir(sourceDrive.filePath("Generation History")).entryList({"*.iisc"}, QDir::Files).isEmpty());
    }
    void historyRepairRestoresPngsAndPreservesAllExistingFiles() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/repair-history-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        const auto stem = QStringLiteral("12345678-1234-1234-1234-123456789abc-0001");
        const auto input = root.filePath("Files/source.png");
        const auto canvas = root.filePath("Generation History/" + stem + ".iisc");
        const auto output = root.filePath("Generation History/" + stem + ".png");
        QImage pixels(7, 13, QImage::Format_ARGB32); pixels.fill(Qt::red); pixels.setPixelColor(3, 9, Qt::blue);
        pixels.setText("parameters", "test prompt\nSeed: 123"); QVERIFY(pixels.save(input));
        QString error;
        const auto legacyDocument = dreamscapes::readImageCanvas(input); QVERIFY(legacyDocument.ok());
        const auto legacy = iiSharedCanvas::encodeIisc(legacyDocument.document); QVERIFY(legacy.ok());
        QFile legacyFile(canvas); QVERIFY(legacyFile.open(QIODevice::WriteOnly));
        QCOMPARE(legacyFile.write(reinterpret_cast<const char *>(legacy.bytes.data()), qint64(legacy.bytes.size())), qint64(legacy.bytes.size())); legacyFile.close();
        QFile native(canvas); QVERIFY(native.open(QIODevice::ReadOnly)); const auto bytes = native.readAll(); native.close();
        const auto run = [&](bool apply) {
            QProcess tool; tool.start(QStringLiteral(DREAMSCAPES_HISTORY_REPAIR), apply ? QStringList{"--apply", root.path()} : QStringList{root.path()});
            if (!tool.waitForStarted() || !tool.waitForFinished(10000) || tool.exitCode() != 0) return QJsonObject{};
            return QJsonDocument::fromJson(tool.readAllStandardOutput()).object();
        };
        const auto inspected = run(false); QVERIFY(!inspected.isEmpty()); QCOMPARE(inspected.value("restored").toInt(), 0);
        QVERIFY(!QFileInfo::exists(output));
        const auto repaired = run(true); QCOMPARE(repaired.value("errors").toInt(-1), 0); QCOMPARE(repaired.value("restored").toInt(), 1);
        const QImage restored(output); QCOMPARE(restored.size(), pixels.size());
        for (int y = 0; y < pixels.height(); ++y) for (int x = 0; x < pixels.width(); ++x) QCOMPARE(restored.pixel(x, y), pixels.pixel(x, y));
        QCOMPARE(restored.text("parameters"), pixels.text("parameters"));
        QVERIFY(native.open(QIODevice::ReadOnly)); QCOMPARE(native.readAll(), bytes); native.close();
        QFile png(output); QVERIFY(png.open(QIODevice::ReadOnly)); const auto pngBytes = png.readAll(); png.close();
        QCOMPARE(run(true).value("restored").toInt(-1), 0);
        QVERIFY(png.open(QIODevice::ReadOnly)); QCOMPARE(png.readAll(), pngBytes);
        QCOMPARE(QDir(root.filePath("Generation History")).entryList({"*.png", "*.iisc"}, QDir::Files).size(), 2);
        QProcess move;
        move.start(QStringLiteral(DREAMSCAPES_HISTORY_REPAIR), {"--move-canvases", root.path()});
        QVERIFY(move.waitForStarted()); QVERIFY(move.waitForFinished(10000)); QCOMPARE(move.exitCode(), 0);
        QCOMPARE(QJsonDocument::fromJson(move.readAllStandardOutput()).object().value("moved").toInt(), 1);
        QVERIFY(!QFileInfo::exists(canvas));
        QFile document(root.filePath("Files/" + stem + ".iisc")); QVERIFY(document.open(QIODevice::ReadOnly)); QCOMPARE(document.readAll(), bytes);
        QCOMPARE(QDir(root.filePath("Generation History")).entryList({"*.png", "*.iisc"}, QDir::Files), QStringList{stem + ".png"});
    }
    void pngBecomesSelfContainedFillCanvas() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/image-wrapper-XXXXXX");
        QImage png(19, 11, QImage::Format_ARGB32); png.fill(QColor(20, 40, 80, 128));
        png.setPixel(0, 0, 0x00123456); png.setPixel(18, 10, 0xffeeddcc);
        const auto source = root.filePath("original %20 #.png"); QVERIFY(png.save(source));
        QFile original(source); QVERIFY(original.open(QIODevice::ReadOnly)); const auto bytes = original.readAll(); original.close();
        QString error;
        const auto path = dreamscapes::persistImageCanvas(source, &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error)); QVERIFY(path.endsWith(".iisc"));
        QVERIFY(path.startsWith(m_storage.filePath("Files/")));
        QVERIFY(QDir(root.path()).entryList({"*.iisc"}, QDir::Files).isEmpty());
        QCOMPARE(dreamscapes::persistImageCanvas(source, &error), path);
        auto native = dreamscapes::readImageCanvas(path);
        QVERIFY2(native.ok(), native.result.message.c_str());
        QCOMPARE(native.document.extent.width, 19); QCOMPARE(native.document.extent.height, 11);
        QCOMPARE(native.document.timeline.frameCount, 1u);
        QCOMPARE(native.document.layers.size(), 1u); QCOMPARE(native.document.assets.size(), 1u);
        const auto &layer = std::get<iiSharedCanvas::StaticBitmapLayer>(native.document.layers.front());
        QCOMPARE(layer.properties.transform.translationX, 0.0);
        QCOMPARE(layer.properties.transform.translationY, 0.0);
        QCOMPARE(layer.properties.transform.m11, 1.0); QCOMPARE(layer.properties.transform.m22, 1.0);
        const auto &pixels = std::get<iiSharedCanvas::RasterAsset>(native.document.assets.front()).pixels;
        QCOMPARE(pixels.pixels.front(), png.pixel(0, 0)); QCOMPARE(pixels.pixels.back(), png.pixel(18, 10));
        QCOMPARE(pixels.pixels[20], png.pixel(1, 1));
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), bytes); original.close();
        QVERIFY(QFile::remove(source));
        const auto preview = dreamscapes::imageCanvasPreview(path, root.path(), &error);
        QVERIFY2(!preview.isEmpty(), qPrintable(error)); QCOMPARE(QImage(preview).size(), png.size());
        QCOMPARE(QImage(preview).pixelColor(1, 1), png.pixelColor(1, 1));
        const auto evidence = qEnvironmentVariable("DREAMSCAPES_IISC_EVIDENCE");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            QVERIFY2(dreamscapes::storeImageCanvas(path, evidence + "/alpha-fill.iisc", &error), qPrintable(error));
            QVERIFY(QImage(preview).save(evidence + "/alpha-preview.png"));
        }
        EditorProject editor; QVERIFY(editor.openImages({QUrl::fromLocalFile(path)}));
        QCOMPARE(editor.currentCanvas()->canvasWidth(), 19);
        QCOMPARE(editor.currentCanvas()->document()->assets.size(), 1u);
        HomeCanvas home; QVERIFY(home.addAttachment(QUrl::fromLocalFile(path)));
        QCOMPARE(home.attachments().first().toMap().value("documentSource").toUrl(), QUrl::fromLocalFile(path));
        const auto refs = home.generationParameters("reference", "model", "1:1", 1).value("referenceImages").toList();
        QCOMPARE(refs.size(), 1); QVERIFY(!QImage(QUrl(refs.front().toString()).toLocalFile()).isNull());
    }
    void wrappersAreRevisionedAndNeverOverwritten() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/image-revision-XXXXXX");
        const auto source = root.filePath("image.png"); QImage image(7, 13, QImage::Format_ARGB32); image.fill(Qt::red);
        QVERIFY(image.save(source)); QString error;
        const auto red = dreamscapes::persistImageCanvas(source, &error); QVERIFY(!red.isEmpty());
        image.fill(Qt::blue); QVERIFY(image.save(source));
        const auto blue = dreamscapes::persistImageCanvas(source, &error); QVERIFY(!blue.isEmpty()); QVERIFY(red != blue);
        QCOMPARE(std::get<iiSharedCanvas::RasterAsset>(dreamscapes::readImageCanvas(red).document.assets[0]).pixels.pixels[0], 0xffff0000u);
        QFile collision(blue); QVERIFY(collision.open(QIODevice::WriteOnly | QIODevice::Truncate)); collision.write("preserve"); collision.close();
        QVERIFY(dreamscapes::persistImageCanvas(source, &error).isEmpty()); QVERIFY(!error.isEmpty());
        QVERIFY(collision.open(QIODevice::ReadOnly)); QCOMPARE(collision.readAll(), QByteArray("preserve"));
        QVERIFY(dreamscapes::persistImageCanvas(root.filePath("missing.png"), &error).isEmpty());
    }
};
QTEST_MAIN(ImageCanvasTests)
#include "tst_imagecanvas.moc"
