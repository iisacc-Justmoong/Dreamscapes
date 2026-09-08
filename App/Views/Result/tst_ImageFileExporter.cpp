#include "ImageFileExporter.h"
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray bytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
}

class ImageFileExporterTests : public QObject
{
    Q_OBJECT
private slots:
    void preservesOriginalBytesAndResolution()
    {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/image-export-XXXXXX");
        QImage image(1200, 800, QImage::Format_ARGB32);
        image.fill(QColor(64, 100, 160, 180));
        image.setText("seed", "12345");
        const auto source = directory.filePath(QStringLiteral("원본 #1 %.png"));
        const auto destination = directory.filePath(QStringLiteral("저장 이미지.png"));
        QVERIFY(image.save(source));
        const auto original = bytes(source);
        ImageFileExporter exporter;
        QSignalSpy saved(&exporter, &ImageFileExporter::saved);
        QSignalSpy failed(&exporter, &ImageFileExporter::failed);
        const auto target = QUrl::fromLocalFile(destination);
        QVERIFY(exporter.save(QUrl::fromLocalFile(source), target));
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved.first().first().toUrl(), target);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(bytes(destination), original);
        QCOMPARE(bytes(source), original);
        QCOMPARE(QImage(destination).size(), image.size());
        QCOMPARE(exporter.suggestedFileName(QUrl::fromLocalFile(source)), QStringLiteral("원본 #1 %.png"));
        // The system save dialog handles overwrite confirmation before calling save().
        QFile old(destination);
        QVERIFY(old.open(QIODevice::WriteOnly | QIODevice::Truncate));
        old.write("previous file");
        old.close();
        QVERIFY(exporter.save(QUrl::fromLocalFile(source), target));
        QCOMPARE(bytes(destination), original);
        QVERIFY(exporter.save(QUrl::fromLocalFile(source), QUrl::fromLocalFile(source)));
        QCOMPARE(bytes(source), original);
    }

    void failuresPreserveExistingFiles()
    {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/image-export-errors-XXXXXX");
        const auto destination = directory.filePath("keep.png");
        QFile keep(destination);
        QVERIFY(keep.open(QIODevice::WriteOnly));
        keep.write("preserve this file");
        keep.close();
        ImageFileExporter exporter;
        QSignalSpy saved(&exporter, &ImageFileExporter::saved);
        QSignalSpy failed(&exporter, &ImageFileExporter::failed);
        const auto target = QUrl::fromLocalFile(destination);
        QVERIFY(!exporter.save(QUrl::fromLocalFile(directory.filePath("missing.png")), target));
        QVERIFY(!exporter.save(QUrl("https://example.com/image.png"), target));
        QVERIFY(!exporter.save(target, target)); // Invalid image bytes must not be accepted.
        QCOMPARE(bytes(destination), QByteArray("preserve this file"));
        const auto source = directory.filePath("source.png");
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(source));
        const auto localSource = QUrl::fromLocalFile(source);
        QVERIFY(!exporter.save(localSource, QUrl()));
        QVERIFY(!exporter.save(localSource, QUrl("https://example.com/save.png")));
        QVERIFY(!exporter.save(localSource, QUrl::fromLocalFile(directory.filePath("missing/output.png"))));
        QVERIFY(!exporter.save(localSource, QUrl::fromLocalFile(directory.path())));
        QCOMPARE(saved.size(), 0);
        QCOMPARE(failed.size(), 7);
        for (const auto &emission : failed) QVERIFY(!emission.first().toString().isEmpty());
        QCOMPARE(QImage(source).size(), image.size());
        QCOMPARE(bytes(destination), QByteArray("preserve this file"));
    }
};

QTEST_GUILESS_MAIN(ImageFileExporterTests)
#include "tst_ImageFileExporter.moc"
