#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include <SocietyDrive.h>
#include <SharedStorage.h>
#include <iiSocietyHelper.h>

class PackagedApplicationTests final : public QObject {
    Q_OBJECT
private slots:
    void packagedApplicationStarts();
};

void PackagedApplicationTests::packagedApplicationStarts()
{
    QTemporaryDir presence(DREAMSCAPES_TEST_DIRECTORY "/helper-XXXXXX");
    QString storageError;
    const auto drive = iiSocietyContainer::SocietyDrive::createAt(presence.path(), &storageError);
    QVERIFY2(drive, qPrintable(storageError));
    const auto containerPath = drive->rootPath();
    const auto storage = iiSocietyContainer::SharedStorage::open(containerPath, &storageError);
    QVERIFY2(storage, qPrintable(storageError));
    const auto helperDirectory = storage->ensureRuntimeDirectory("helpers/test", &storageError);
    QVERIFY2(!helperDirectory.isEmpty(), qPrintable(storageError));
    const auto endpointsDirectory = storage->ensureRuntimeDirectory("apps/test", &storageError);
    QVERIFY2(!endpointsDirectory.isEmpty(), qPrintable(storageError));
    iiSocietyHelper::Helper observer;
    QVERIFY(observer.start({"com.iisacc.dreamscapes.test", "Dreamscapes test", "1"},
                            {helperDirectory, 100, 5000}));
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("SOCIETY_CONTAINER_PATH", containerPath);
    environment.insert("SOCIETY_HELPER_DIRECTORY", helperDirectory);
    environment.insert("IILOCALLLM_APP_ENDPOINTS", endpointsDirectory);
    for (const auto *key : {"DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"})
        environment.remove(QString::fromLatin1(key));
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    // Verify the installed entry point too, without copying another build-tree app.
    const auto installedExecutable = qEnvironmentVariable("DREAMSCAPES_TEST_APP_PATH");
    process.start(installedExecutable.isEmpty() ? QStringLiteral(DREAMSCAPES_EXECUTABLE_PATH)
                                               : installedExecutable,
                  {"--society-container", containerPath});
    QVERIFY(process.waitForStarted());
    QByteArray output;
    QElapsedTimer timer;
    timer.start();
    while (!output.contains("LVRS bootstrap.entry.root-loaded")
           && !output.contains("LVRS bootstrap.entry.root-load-failed") && timer.elapsed() < 90000
           && process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        output += process.readAll();
    }
    const bool running = process.state() == QProcess::Running;
    // Both participants must actually observe each other through the installed SDK.
    QElapsedTimer discovery;
    discovery.start();
    while (!output.contains("LVRS bootstrap.entry.root-load-failed")
           && process.state() == QProcess::Running && discovery.elapsed() < 15000 && (observer.peers().isEmpty()
           || !output.contains("com.iisacc.dreamscapes observed com.iisacc.dreamscapes.test"))) {
        QTest::qWait(50);
        output += process.readAll();
    }
    const auto peers = observer.peers();
    process.terminate();
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished();
    }
    output += process.readAll();
    // Source GUI fixtures register their own types. Only this real entry-point
    // check catches a missing production registration in the static module.
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        QVERIFY(QDir().mkpath(capture));
        QFile entryLog(capture+"/packaged-entry.log");
        QVERIFY(entryLog.open(QIODevice::WriteOnly));
        QCOMPARE(entryLog.write(output),qint64(output.size()));
    }
    QVERIFY2(!output.contains("LVRS bootstrap.entry.root-load-failed"), output.constData());
    QVERIFY2(running, output.constData());
    QVERIFY2(output.contains("LVRS bootstrap.entry.root-loaded"), output.constData());
    QVERIFY2(peers.size() == 1, output.constData());
    QCOMPARE(peers.first().application.id, "com.iisacc.dreamscapes");
    QVERIFY2(output.contains("com.iisacc.dreamscapes observed com.iisacc.dreamscapes.test"), output.constData());
    QVERIFY2(output.contains("\"windowCount\":1"), output.constData());
    QVERIFY2(!output.contains("failed to load") && !output.contains("is not installed")
        && !output.contains("QML Image: Cannot open:"), output.constData());
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication application(argc, argv);
    application.setApplicationName("DreamscapesGuiTests");
    QTemporaryDir settings(DREAMSCAPES_TEST_DIRECTORY "/gui-storage-XXXXXX");
    if (!settings.isValid()) return 1;
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH", settings.filePath("storage.json").toUtf8());
    qunsetenv("SOCIETY_CONTAINER_PATH");
    qputenv("IILD_GENERATOR_EXECUTABLE", DREAMSCAPES_FAKE_GENERATOR);
    qputenv("DREAMSCAPES_TEMP_DIRECTORY", DREAMSCAPES_TEST_DIRECTORY);
    qputenv("SOCIETY_DISABLE_SESSION_RESTORE", "1");
    qputenv("DREAMSCAPES_TEST_PRESET_FILE", settings.filePath("advanced-presets.json").toUtf8());
    PackagedApplicationTests tests;
    QTimer::singleShot(0, &application, [&] {
        application.exit(QTest::qExec(&tests, argc, argv));
    });
    return application.exec();
}

#include "tst_packagedapplication.moc"
