#include "ImageFileExporter.h"
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <SocietyDrive.h>
#include <iiSocietyHelper/MediaExport.h>
#include <QtTest>
#include <QTimer>
#include <QPointer>
#include <semaphore>
#include <atomic>
#include <future>
#include <thread>

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
    void terminalOutcomePrecedesIdleDeletion_data() {
        QTest::addColumn<bool>("failure");QTest::newRow("saved")<<false;QTest::newRow("failed")<<true;
    }
    void terminalOutcomePrecedesIdleDeletion() {
        QFETCH(bool,failure);QStringList order;
        QPointer<ImageFileExporter> exporter=new ImageFileExporter([failure](const QUrl &,const QUrl &,bool){return failure?QString("Fixture failure"):QString{};},nullptr);
        connect(exporter,&ImageFileExporter::saved,this,[&]{order.append("saved");});
        connect(exporter,&ImageFileExporter::failed,this,[&]{order.append("failed");});
        connect(exporter,&ImageFileExporter::busyChanged,this,[&]{if(exporter&&!exporter->busy()){order.append("idle");delete exporter.data();}});
        QVERIFY(exporter->save(QUrl("file:///source.png"),QUrl("file:///destination.png")));
        QTRY_VERIFY(exporter.isNull());QCOMPARE(order,QStringList({failure?"failed":"saved","idle"}));
    }
    void reentrantCompletionDoesNotPublishThePreviousIdleState() {
        auto gate=std::make_shared<std::binary_semaphore>(0);auto entered=std::make_shared<std::atomic_bool>(false);
        const auto unblock=qScopeGuard([gate]{gate->release();});
        ImageFileExporter exporter([gate,entered](const QUrl &,const QUrl &destination,bool){
            if(destination.path().endsWith("second.png")){entered->store(true);if(!gate->try_acquire_for(std::chrono::seconds(5)))return QString("Gate timed out");}
            return QString{};
        },nullptr);
        const QUrl source("file:///source.png"),first("file:///first.png"),second("file:///second.png");
        QSignalSpy saved(&exporter,&ImageFileExporter::saved),busy(&exporter,&ImageFileExporter::busyChanged);
        connect(&exporter,&ImageFileExporter::saved,this,[&](const QUrl &destination){if(destination==first)QVERIFY(exporter.save(source,second));});
        QVERIFY(exporter.save(source,first));QTRY_VERIFY(entered->load());
        QCOMPARE(saved.count(),1);QVERIFY(exporter.busy());QCOMPARE(busy.count(),2);
        gate->release();QTRY_COMPARE(saved.count(),2);QCOMPARE(busy.count(),3);
    }
    void acceptedExportCompletesAfterReturningToTheEventLoop()
    {
        QTemporaryDir drive(DREAMSCAPES_TEST_DIRECTORY "/export-drive-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(drive.path()));
        const auto previous = qgetenv("SOCIETY_CONTAINER_PATH");
        qputenv("SOCIETY_CONTAINER_PATH", drive.path().toUtf8());
        const auto restore = qScopeGuard([&] {
            if (previous.isNull()) qunsetenv("SOCIETY_CONTAINER_PATH");
            else qputenv("SOCIETY_CONTAINER_PATH", previous);
        });
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/async-export-XXXXXX");
        const auto source = directory.filePath("source.png"), target = directory.filePath("saved.png");
        QImage image(8, 4, QImage::Format_ARGB32); image.fill(Qt::red); QVERIFY(image.save(source));
        ImageFileExporter exporter;
        QSignalSpy saved(&exporter, &ImageFileExporter::saved);
        QVERIFY(exporter.save(QUrl::fromLocalFile(source), QUrl::fromLocalFile(target)));
        QCOMPARE(saved.size(), 0); // Acceptance must return before copy/completion.
        QTRY_COMPARE(saved.size(), 1);
        QCOMPARE(bytes(target), bytes(source));
    }
    void exportsOnlyRecognizedMp4Bytes()
    {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/video-export-XXXXXX");
        const auto source=directory.filePath("source.mp4"), target=directory.filePath("saved.mp4");
        QFile video(source); QVERIFY(video.open(QIODevice::WriteOnly));
        const QByteArray content=QByteArray::fromHex("000000206674797069736f6d0000020069736f6d69736f32617663316d703431");
        video.write(content); video.close();
        ImageFileExporter exporter;
        QSignalSpy saved(&exporter, &ImageFileExporter::saved), failed(&exporter, &ImageFileExporter::failed);
        QVERIFY(exporter.saveVideo(QUrl::fromLocalFile(source),QUrl::fromLocalFile(target)));
        QTRY_COMPARE(saved.size(), 1);
        QCOMPARE(bytes(target),content);
        QVERIFY(exporter.save(QUrl::fromLocalFile(source),QUrl::fromLocalFile(target)));
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(video.open(QIODevice::WriteOnly)); video.write("invalid video"); video.close();
        QVERIFY(exporter.saveVideo(QUrl::fromLocalFile(source),QUrl::fromLocalFile(target)));
        QTRY_COMPARE(failed.size(), 2);
        QCOMPARE(bytes(target),content);
    }
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
        QTRY_COMPARE(saved.size(), 1);
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
        QTRY_COMPARE(saved.size(), 2);
        QCOMPARE(bytes(destination), original);
        QVERIFY(exporter.save(QUrl::fromLocalFile(source), QUrl::fromLocalFile(source)));
        QTRY_COMPARE(saved.size(), 3);
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
        QSignalSpy rejected(&exporter, &ImageFileExporter::rejected);
        const auto target = QUrl::fromLocalFile(destination);
        QVERIFY(exporter.save(QUrl::fromLocalFile(directory.filePath("missing.png")), target));
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(!exporter.save(QUrl("https://example.com/image.png"), target));
        QCOMPARE(rejected.size(), 1);QCOMPARE(failed.size(), 1);
        QVERIFY(exporter.save(target, target)); // Validation completes on the worker.
        QTRY_COMPARE(failed.size(), 2);
        QCOMPARE(bytes(destination), QByteArray("preserve this file"));
        const auto source = directory.filePath("source.png");
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(source));
        const auto localSource = QUrl::fromLocalFile(source);
        QVERIFY(!exporter.save(localSource, QUrl()));
        QVERIFY(!exporter.save(localSource, QUrl("https://example.com/save.png")));
        QVERIFY(exporter.save(localSource, QUrl::fromLocalFile(directory.filePath("missing/output.png"))));
        QTRY_COMPARE(failed.size(), 3);
        QVERIFY(exporter.save(localSource, QUrl::fromLocalFile(directory.path())));
        QTRY_COMPARE(failed.size(), 4);
        QCOMPARE(saved.size(), 0);
        QCOMPARE(failed.size(), 4);QCOMPARE(rejected.size(), 3);
        for (const auto &emission : failed) QVERIFY(!emission.first().toString().isEmpty());
        QCOMPARE(QImage(source).size(), image.size());
        QCOMPARE(bytes(destination), QByteArray("preserve this file"));
    }
    void slowCopyDoesNotBlockInputAndRejectsDuplicateWork()
    {
        auto release = std::make_shared<std::promise<void>>();
        const auto gate = release->get_future().share();
        auto started = std::make_shared<std::atomic_bool>(false);
        auto worker = std::make_shared<std::thread::id>();
        const auto owner = std::this_thread::get_id();
        auto unblock = qScopeGuard([&] { release->set_value(); });
        ImageFileExporter exporter([gate, started, worker](const QUrl &, const QUrl &, bool) {
            *worker = std::this_thread::get_id(); started->store(true);
            gate.wait(); return QString{};
        }, nullptr);
        QSignalSpy saved(&exporter, &ImageFileExporter::saved), failed(&exporter, &ImageFileExporter::failed);
        QSignalSpy rejected(&exporter,&ImageFileExporter::rejected);
        int inputs = 0; QTimer input; input.setInterval(1);
        connect(&input, &QTimer::timeout, this, [&] { ++inputs; }); input.start();
        const auto source = QUrl::fromLocalFile(DREAMSCAPES_TEST_DIRECTORY "/queued-source.png");
        const auto target = QUrl::fromLocalFile(DREAMSCAPES_TEST_DIRECTORY "/queued-target.png");
        QVERIFY(exporter.save(source, target)); QVERIFY(exporter.busy());
        QTRY_VERIFY(started->load()); QVERIFY(*worker != owner);
        QVERIFY(!exporter.saveVideo(source, target)); QCOMPARE(rejected.size(), 1);
        QTRY_VERIFY(inputs >= 3); QCOMPARE(saved.size(), 0); QVERIFY(exporter.busy());
        release->set_value(); unblock.dismiss();
        QTRY_COMPARE(saved.size(), 1); QVERIFY(!exporter.busy());
        QCOMPARE(saved.first().first().toUrl(), target);
        QCOMPARE(failed.size(), 0);
        QVERIFY(exporter.save(source, target));
        QTRY_COMPARE(saved.size(), 2); QVERIFY(!exporter.busy());
    }
    void closingTheViewPreservesAcceptedCopy_data()
    {
        QTest::addColumn<bool>("closeFromNotification");
        QTest::newRow("after-acceptance") << false;
        QTest::newRow("inside-busy-notification") << true;
    }
    void closingTheViewPreservesAcceptedCopy()
    {
        QFETCH(bool, closeFromNotification);
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/closed-export-XXXXXX");
        const auto source = QUrl::fromLocalFile(directory.filePath("source.png"));
        const auto target = QUrl::fromLocalFile(directory.filePath("saved.png"));
        QImage image(64, 32, QImage::Format_RGB32); image.fill(Qt::green); QVERIFY(image.save(source.toLocalFile()));
        auto release = std::make_shared<std::promise<void>>();
        const auto gate = release->get_future().share();
        auto started = std::make_shared<std::atomic_bool>(false);
        auto completed = std::make_shared<std::promise<QString>>();
        auto outcome = completed->get_future();
        auto unblock = qScopeGuard([&] { release->set_value(); });
        auto exporter = std::make_unique<ImageFileExporter>([gate, started, completed](const QUrl &input, const QUrl &output, bool video) {
            started->store(true); gate.wait();
            const auto result = iiSocietyHelper::exportMedia(input, output, video);
            completed->set_value(result.error);
            return result.error;
        }, nullptr);
        if (closeFromNotification) connect(exporter.get(), &ImageFileExporter::busyChanged, this, [&] {
            if (exporter && exporter->busy()) exporter.reset();
        });
        QVERIFY(exporter->save(source, target));
        if (closeFromNotification) QVERIFY(!exporter);
        QTRY_VERIFY(started->load());
        exporter.reset(); // Destruction must return while the copy is still gated.
        QVERIFY(!QFileInfo::exists(target.toLocalFile()));
        release->set_value(); unblock.dismiss();
        QTRY_VERIFY(outcome.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        QVERIFY(outcome.get().isEmpty());
        QCOMPARE(bytes(target.toLocalFile()), bytes(source.toLocalFile()));
    }
};

QTEST_GUILESS_MAIN(ImageFileExporterTests)
#include "tst_ImageFileExporter.moc"
