#include "PhotoLibraryExporter.h"
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <thread>

class PhotoLibraryExporterTests : public QObject
{
    Q_OBJECT
private slots:
    void savesOriginalAsynchronouslyAndPreventsDuplicates()
    {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/photo-library-XXXXXX");
        QImage image(1200, 800, QImage::Format_RGB32);
        image.fill(Qt::cyan);
        image.setText("seed", "987654");
        const auto path = directory.filePath(QStringLiteral("원본 #1.png"));
        QVERIFY(image.save(path));
        PhotoLibraryExporter::Completion finish;
        QString imported;
        int requests = 0;
        PhotoLibraryExporter exporter([&](const QString &source, auto completion) {
            imported = source;
            finish = completion;
            ++requests;
        }, nullptr);
        QSignalSpy busy(&exporter, &PhotoLibraryExporter::busyChanged);
        QSignalSpy saved(&exporter, &PhotoLibraryExporter::saved);
        QSignalSpy failed(&exporter, &PhotoLibraryExporter::failed);
        QVERIFY(exporter.supported());
        QVERIFY(exporter.save(QUrl::fromLocalFile(path)));
        QVERIFY(exporter.busy());
        QCOMPARE(imported, path);
        QCOMPARE(QImage(imported).text("seed"), QString("987654"));
        QVERIFY(!exporter.save(QUrl::fromLocalFile(path)));
        QCOMPARE(requests, 1);
        QCOMPARE(saved.size(), 0);
        // PhotoKit and Android invoke the result on a background thread.
        std::thread worker([&] { finish("native-library-asset", {}); });
        worker.join();
        QTRY_COMPARE(saved.size(), 1);
        QVERIFY(!exporter.busy());
        QCOMPARE(saved.first().first().toString(), QString("native-library-asset"));
        QCOMPARE(failed.size(), 0);
        QCOMPARE(busy.size(), 2);
        QCOMPARE(QImage(path).size(), image.size());

        QVERIFY(exporter.save(QUrl::fromLocalFile(path)));
        finish({}, "Permission was denied.");
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(!exporter.busy());
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Permission was denied."));
        QVERIFY(exporter.save(QUrl::fromLocalFile(path))); // A failed import can be retried.
        finish({}, {});
        QTRY_COMPARE(failed.size(), 2);
        QCOMPARE(saved.size(), 1); // A missing asset identifier is never a success.
    }

    void rejectsUnsupportedAndUnreadableSources()
    {
        PhotoLibraryExporter unsupported({}, nullptr);
        QSignalSpy unsupportedFailure(&unsupported, &PhotoLibraryExporter::failed);
        QVERIFY(!unsupported.supported());
        QVERIFY(!unsupported.save(QUrl("https://example.com/image.png")));
        QCOMPARE(unsupportedFailure.size(), 1);
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/photo-invalid-XXXXXX");
        const auto path = directory.filePath("invalid.png");
        QFile invalid(path);
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("not an image");
        invalid.close();
        int calls = 0;
        PhotoLibraryExporter exporter([&](const QString &, auto) { ++calls; }, nullptr);
        QSignalSpy failed(&exporter, &PhotoLibraryExporter::failed);
        for (const auto &url : {QUrl(), QUrl("https://example.com/image.png"),
             QUrl::fromLocalFile(directory.path()), QUrl::fromLocalFile(path),
             QUrl::fromLocalFile(directory.filePath("missing.png"))}) {
            QVERIFY(!exporter.save(url));
            QVERIFY(!exporter.busy());
        }
        QCOMPARE(calls, 0);
        QCOMPARE(failed.size(), 5);
    }

    void callbackSurvivesClosedView()
    {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/photo-lifetime-XXXXXX");
        QImage image(16, 16, QImage::Format_RGB32);
        image.fill(Qt::blue);
        const auto path = directory.filePath("image.png");
        QVERIFY(image.save(path));
        PhotoLibraryExporter::Completion finish;
        auto *exporter = new PhotoLibraryExporter([&](const QString &, auto callback) { finish = callback; }, nullptr);
        QVERIFY(exporter->save(QUrl::fromLocalFile(path)));
        delete exporter;
        finish("completed-after-close", {});
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    }
};

QTEST_GUILESS_MAIN(PhotoLibraryExporterTests)
#include "tst_PhotoLibraryExporter.moc"
