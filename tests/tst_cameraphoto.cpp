#include "App/Views/Editor/EditorCameraPhoto.h"
#include "App/Views/Editor/EditorPhotoImageNative.h"
#include <QSignalSpy>
#include <QImageReader>
#include <QMediaDevices>
#include <QPointer>
#include <QScopeGuard>
#include <QFileInfo>
#include <QDir>
#include <QtTest>
#include <QAbstractItemModel>
#include <QAbstractItemModelTester>
#include <stdexcept>
#include <thread>

namespace {
class PhotoFixture final : public iiPhotoLibrary::PhotoLibrary {
public:
    iiPhotoLibrary::Access permission = iiPhotoLibrary::Access::Full;
    int authorizationRequests = 0;
    int previews = 0, exports = 0, scans = 0;
    bool deferAuthorization = false;
    std::function<void()> authorizationReply;
    std::function<void(std::function<void()>)> authorization;
    QString name() const override { return "Fixture"; }
    iiPhotoLibrary::Access access() const override { return permission; }
    void authorize(std::function<void()> done) override {
        ++authorizationRequests;
        if (authorization) { authorization(std::move(done)); return; }
        if (deferAuthorization) authorizationReply = std::move(done);
        else done();
    }
    iiPhotoLibrary::Snapshot scan(QString *) override {
        ++scans;
        iiPhotoLibrary::Snapshot snapshot; snapshot.complete = true;
        for (int i = 0; i < 3; ++i) {
            iiPhotoLibrary::Asset asset; asset.identifier = QString::number(i); asset.name = QStringList{"Coast.png", "Studio.png", "Capture.png"}[i];
            asset.media = "photo"; asset.stamp = QString("0:%1:32").arg(64 * (i + 1));
            asset.created = QDateTime::fromSecsSinceEpoch(1000 + i * 1000);
            asset.resources.append({"original", asset.name, "photo"}); snapshot.assets.append(asset);
        }
        return snapshot;
    }
    bool preview(const QString &, const QString &path, QString *) override {
        ++previews;
        QImage image(20, 10, QImage::Format_ARGB32); image.fill(Qt::blue); return image.save(path);
    }
    bool exportResource(const QString &, const QString &token, const QString &path, QString *) override {
        ++exports;
        if (token != "original") return false;
        QImage image(160, 80, QImage::Format_ARGB32); image.fill(Qt::green); return image.save(path);
    }
    QString import(const QJsonObject &, const QStringList &, QString *) override { return "saved"; }
    bool trash(const QString &, QString *) override { return false; }
};
}
class CameraPhotoTests : public QObject {
    Q_OBJECT
private slots:
    void passiveViewsAndGalleryDoNotAcquireDevicesOrOutputStorage();
    void explicitCameraQueriesInitializeOneMonitorWithoutCreatingOutputs();
    void nativeThumbnailAndOriginalAllocateStorageOnlyOnRequest();
    void unmodifiedLocalImportDoesNotAllocateOutputStorage();
    void outputStorageFailureIsLocalAndRetryable_data();
    void outputStorageFailureIsLocalAndRetryable();
    void repeatedStorageFailureDoesNotReenterGalleryNotifications();
    void cancelledCaptureDoesNotAllocateStorage();
    void transformsFullResolutionPixels();
    void nativeImageDecoderRetainsFullResolution();
    void galleryUsesOriginalsAndSelectionScope();
    void deniedLibraryDoesNotInventPhotos();
    void galleryPermissionWaitIsExplicitAndClearsAcquisitionErrors();
    void captureWaitsForImportAndCancelsOldCallbacks();
    void timerCancellationPreventsCapture();
    void galleryRowsUpdateWithoutReset_data();
    void galleryRowsUpdateWithoutReset();
    void galleryFilterRetainsOrderAndSkipsIdenticalRequests();
    void galleryRowObserverCanChangeTheFilter();
    void galleryRowObserverCanDeleteTheOwner();
    void galleryErrorObserverKeepsTheNewestRequest_data();
    void galleryErrorObserverKeepsTheNewestRequest();
    void catalogResetPreservesTheThumbnailPreparationError();
    void galleryAuthorizationDoesNotDispatchForAReplacedView();
    void galleryAuthorizationCompletionUsesTheCurrentPhotosView_data();
    void galleryAuthorizationCompletionUsesTheCurrentPhotosView();
    void galleryAuthorizationCompletionPreservesALocalCatalog();
    void galleryAuthorizationOldReplyCannotRetireANewerRequest();
    void galleryAuthorizationReplyIsConsumedOnce();
    void galleryAuthorizationRechecksAccessBeforeDispatch_data();
    void galleryAuthorizationRechecksAccessBeforeDispatch();
    void galleryAuthorizationOwnerCanClose_data();
    void galleryAuthorizationOwnerCanClose();
    void galleryAuthorizationFailureCanRetry_data();
    void galleryAuthorizationFailureCanRetry();
    void galleryAuthorizationFailurePreservesANewerLocalView();
    void galleryAuthorizationCompletionReturnsToTheOwnerThread();
    void galleryAuthorizationCanCompleteSynchronously();
    void galleryAuthorizationSurvivesARefreshOfTheSamePhotosView();
    void galleryPresenterEndDefersAuthorizedReads_data();
    void galleryPresenterEndDefersAuthorizedReads();
    void galleryPresenterRetirementBeforeDispatchSkipsAuthorization();
    void staleGalleryPresenterCannotRetireTheCurrentView();
    void retiredGalleryDoesNotAcquireAThumbnail();
    void reattachedGalleryCanAcquireFromItsCachedCatalog();
};

void CameraPhotoTests::galleryRowsUpdateWithoutReset_data() {
    QTest::addColumn<QString>("action");
    QTest::newRow("selection") << QString("selection");
    QTest::newRow("thumbnail") << QString("thumbnail");
    QTest::newRow("import-status") << QString("import-status");
}
void CameraPhotoTests::galleryRowsUpdateWithoutReset() {
    QFETCH(QString, action); auto library = std::make_shared<PhotoFixture>(); EditorCameraPhoto source(library, nullptr);
    source.loadGallery("iCloud Photo"); QTRY_COMPARE(source.gallery().size(), 3);
    auto *model = qobject_cast<QAbstractItemModel *>(source.property("galleryModel").value<QObject *>()); QVERIFY(model);
    QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(model->rowCount(), 3); const int role = model->roleNames().key("entry", -1); QVERIFY(role >= 0);
    if (action == "import-status") source.setSelected("0", false);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset), changed(model, &QAbstractItemModel::dataChanged);
    if (action == "selection") {
        source.setSelected("0", false); QCOMPARE(source.selectionCount(), 1);
        QVERIFY(model->data(model->index(2, 0), role).toMap()["selected"].toBool());
    } else if (action == "thumbnail") {
        source.requestThumbnail("0"); QTRY_VERIFY(!model->data(model->index(2, 0), role).toMap()["thumbnail"].toUrl().isEmpty());
        QCOMPARE(model->data(model->index(2, 0), role).toMap()["thumbnail"], source.gallery().last().toMap()["thumbnail"]);
    } else {
        QSignalSpy imported(&source, &EditorCameraPhoto::importReady); QVERIFY(source.importSelection()); QTRY_COMPARE(imported.count(), 1);
        QCOMPARE(changed.count(), 0);
    }
    QCOMPARE(resets.count(), 0);
    for (const auto &event : changed) { QCOMPARE(event[0].value<QModelIndex>().row(), 2); QCOMPARE(event[1].value<QModelIndex>().row(), 2); }
    if (action != "import-status") QVERIFY(!changed.isEmpty());
}
void CameraPhotoTests::galleryFilterRetainsOrderAndSkipsIdenticalRequests() {
    EditorCameraPhoto source(std::make_shared<PhotoFixture>(), nullptr); source.loadGallery("iCloud Photo"); QTRY_COMPARE(source.gallery().size(), 3);
    auto *model = qobject_cast<QAbstractItemModel *>(source.property("galleryModel").value<QObject *>()); QVERIFY(model);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset); source.filterGallery("", "Name"); QCOMPARE(resets.count(), 1);
    QCOMPARE(source.gallery()[0].toMap()["id"].toString(), QString("2")); QCOMPARE(source.gallery()[1].toMap()["id"].toString(), QString("0"));
    resets.clear(); QSignalSpy notifications(&source, &EditorCameraPhoto::galleryChanged); source.filterGallery("", "Name"); QCOMPARE(resets.count(), 0); QCOMPARE(notifications.count(), 0);
    source.setSelected("1", true); source.filterGallery("Coast", "Name"); QCOMPARE(model->rowCount(), 1); source.selectAll(true);
    QCOMPARE(source.selectionCount(), 1); QCOMPARE(source.gallery().first().toMap()["id"].toString(), QString("0")); QVERIFY(source.gallery().first().toMap()["selected"].toBool());
}
void CameraPhotoTests::galleryRowObserverCanChangeTheFilter() {
    EditorCameraPhoto source(std::make_shared<PhotoFixture>(), nullptr); source.loadGallery("iCloud Photo"); QTRY_COMPARE(source.gallery().size(), 3);
    auto *model = qobject_cast<QAbstractItemModel *>(source.property("galleryModel").value<QObject *>()); QVERIFY(model);
    connect(model, &QAbstractItemModel::dataChanged, this, [&] { source.filterGallery("Studio", "Name"); });
    source.setSelected("0", false); QCOMPARE(model->rowCount(), 1); QCOMPARE(source.gallery().first().toMap()["id"].toString(), QString("1")); QCOMPARE(source.selectionCount(), 1);
}
void CameraPhotoTests::galleryRowObserverCanDeleteTheOwner() {
    auto source = std::make_unique<EditorCameraPhoto>(std::make_shared<PhotoFixture>(), nullptr); source->loadGallery("iCloud Photo"); QTRY_COMPARE(source->gallery().size(), 3);
    auto *model = qobject_cast<QAbstractItemModel *>(source->property("galleryModel").value<QObject *>()); QVERIFY(model);
    QPointer<QObject> observed(model); connect(model, &QAbstractItemModel::dataChanged, this, [&] { source.reset(); });
    source->setSelected("0", false); QVERIFY(!source); QVERIFY(observed.isNull());
}
void CameraPhotoTests::galleryErrorObserverKeepsTheNewestRequest_data() {
    QTest::addColumn<bool>("remove"); QTest::newRow("delete-owner") << true; QTest::newRow("replace-request") << false;
}
void CameraPhotoTests::galleryErrorObserverKeepsTheNewestRequest() {
    QFETCH(bool, remove); QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/gallery-observer-XXXXXX"); QVERIFY(directory.isValid());
    auto library = std::make_shared<PhotoFixture>(); auto source = std::make_unique<EditorCameraPhoto>(library, nullptr); source->reportImportFailure("Previous acquisition error");
    bool observed = false; connect(source.get(), &EditorCameraPhoto::changed, this, [&] {
        if (observed || !source->error().isEmpty()) return; observed = true;
        if (remove) source.reset(); else source->loadGallery("Local folder", QUrl::fromLocalFile(directory.path()));
    });
    source->loadGallery("iCloud Photo"); QVERIFY(observed);
    if (remove) QVERIFY(!source); else { QTRY_VERIFY(!source->galleryBusy()); QVERIFY(source->gallery().isEmpty()); }
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000)); QCOMPARE(library->scans, 0);
}
void CameraPhotoTests::catalogResetPreservesTheThumbnailPreparationError() {
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/gallery-reset-error-XXXXXX"); QVERIFY(directory.isValid());
    const auto previous = qgetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE");
    const auto restore = qScopeGuard([&] { if (previous.isNull()) qunsetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); else qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", previous); });
    const auto path = directory.filePath("blocked"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("preserve"); file.close(); qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", path.toUtf8());
    auto library = std::make_shared<PhotoFixture>(); EditorCameraPhoto source(library, nullptr);
    auto *model = qobject_cast<QAbstractItemModel *>(source.property("galleryModel").value<QObject *>()); QVERIFY(model);
    connect(model, &QAbstractItemModel::modelReset, this, [&] { if (model->rowCount()) { QVERIFY(!source.galleryBusy()); source.requestThumbnail("0"); } });
    source.loadGallery("iCloud Photo"); QTRY_COMPARE(source.gallery().size(), 3);
    QVERIFY(!source.galleryStatus().isEmpty()); QCOMPARE(source.gallery().last().toMap()["previewError"].toString(), source.galleryStatus()); QCOMPARE(library->previews, 0);
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("preserve"));
}
void CameraPhotoTests::passiveViewsAndGalleryDoNotAcquireDevicesOrOutputStorage() {
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/photo-lazy-XXXXXX"); QVERIFY(directory.isValid());
    const auto previous = qgetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE");
    const auto restore = qScopeGuard([&] { if (previous.isNull()) qunsetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); else qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", previous); });
    const auto root = directory.filePath("output"); qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", root.toUtf8());
    int deviceMonitors = 0; auto library = std::make_shared<PhotoFixture>();
    {
        EditorCameraPhoto sources(library, {}, [&] { ++deviceMonitors; return std::unique_ptr<QMediaDevices>{}; }, nullptr);
        QCOMPARE(deviceMonitors, 0); QVERIFY(!QFileInfo::exists(root));
        QVERIFY(sources.error().isEmpty()); QVERIFY(!sources.busy()); QVERIFY(!sources.previewActive());
        QVERIFY(!sources.recording()); QCOMPARE(sources.recordingDuration(), 0); QCOMPARE(sources.countdown(), 0);
        QVERIFY(sources.currentCapture().isEmpty()); QVERIFY(sources.captureThumbnail().isEmpty()); QVERIFY(sources.captureTargets().isEmpty());
        sources.attachPreview(nullptr); sources.setPreviewEnabled(false, {}); sources.cancel();
        sources.loadGallery("iCloud Photo"); QTRY_COMPARE(sources.gallery().size(), 3);
        sources.filterGallery("Coast", "Name"); sources.selectAll(true); sources.retainSingleSelection();
        QCOMPARE(sources.selectionCount(), 1); QCOMPARE(library->authorizationRequests, 0);
        QCOMPARE(library->previews, 0); QCOMPARE(library->exports, 0);
        QCOMPARE(deviceMonitors, 0); QVERIFY(!QFileInfo::exists(root));
    }
    QCOMPARE(deviceMonitors, 0); QVERIFY(!QFileInfo::exists(root));
}
void CameraPhotoTests::nativeThumbnailAndOriginalAllocateStorageOnlyOnRequest() {
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/photo-native-outputs-XXXXXX"); QVERIFY(directory.isValid());
    const auto previous = qgetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE");
    const auto restore = qScopeGuard([&] { if (previous.isNull()) qunsetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); else qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", previous); });
    const auto root = directory.filePath(QString::fromUtf8("출력-images")); qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", root.toUtf8());
    auto library = std::make_shared<PhotoFixture>(); QString cache;
    {
        EditorCameraPhoto sources(library, nullptr);
        sources.loadGallery("iCloud Photo"); QTRY_COMPARE(sources.gallery().size(), 3);
        QVERIFY(!QFileInfo::exists(root));
        sources.requestThumbnail("0");
        QTRY_VERIFY(!sources.gallery().last().toMap()["thumbnail"].toUrl().isEmpty());
        const auto thumbnail = sources.gallery().last().toMap()["thumbnail"].toUrl().toLocalFile();
        cache = QFileInfo(thumbnail).absolutePath(); QVERIFY(cache.startsWith(root + '/'));
        QCOMPARE(QImage(thumbnail).size(), QSize(20, 10)); QCOMPARE(library->previews, 1);
        sources.requestThumbnail("0"); QCOMPARE(library->previews, 1);
        sources.setSelected("0", false); QSignalSpy imported(&sources, &EditorCameraPhoto::importReady);
        QVERIFY(sources.importSelection()); QTRY_COMPARE(imported.count(), 1); QCOMPARE(library->exports, 1);
        const auto original = imported[0][0].toUrl().toLocalFile(); QVERIFY(original.startsWith(cache + '/'));
        QCOMPARE(QImage(original).size(), QSize(160, 80)); QVERIFY(QFileInfo::exists(cache));
    }
    QVERIFY(!QFileInfo::exists(cache));
}
void CameraPhotoTests::explicitCameraQueriesInitializeOneMonitorWithoutCreatingOutputs() {
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/photo-devices-XXXXXX"); QVERIFY(directory.isValid());
    const auto previous = qgetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE");
    const auto restore = qScopeGuard([&] { if (previous.isNull()) qunsetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); else qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", previous); });
    const auto root = directory.filePath("output"); qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", root.toUtf8());
    int devices = 0; QPointer<QMediaDevices> monitor;
    {
        EditorCameraPhoto sources(std::make_shared<PhotoFixture>(), {}, [&] {
            ++devices; auto result = std::make_unique<QMediaDevices>(); monitor = result.get(); return result;
        }, nullptr);
        QCOMPARE(devices, 0); QSignalSpy changed(&sources, &EditorCameraPhoto::changed);
        (void)sources.devices(); QCOMPARE(devices, 1); QVERIFY(monitor);
        for (int i = 0; i < 10; ++i) {
            (void)sources.devices(); (void)sources.deviceId(); (void)sources.cameraAvailable();
            (void)sources.cameraStatus(); (void)sources.cameraDescription(); sources.refreshDevices();
        }
        const auto before = changed.count();
        QVERIFY(QMetaObject::invokeMethod(monitor, "videoInputsChanged", Qt::DirectConnection));
        QVERIFY(changed.count() > before); QCOMPARE(devices, 1); QVERIFY(!QFileInfo::exists(root));
    }
    QVERIFY(!monitor); QCOMPARE(devices, 1); QVERIFY(!QFileInfo::exists(root));
}
void CameraPhotoTests::unmodifiedLocalImportDoesNotAllocateOutputStorage() {
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/photo-local-import-XXXXXX"); QVERIFY(directory.isValid());
    const auto previous = qgetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE");
    const auto restore = qScopeGuard([&] { if (previous.isNull()) qunsetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); else qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", previous); });
    const auto root = directory.filePath("output"); qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", root.toUtf8());
    const auto path = directory.filePath("input.png"); QImage image(50, 30, QImage::Format_ARGB32); image.fill(Qt::yellow); QVERIFY(image.save(path));
    EditorCameraPhoto sources(std::make_shared<PhotoFixture>(), nullptr);
    sources.loadGallery("Local folder", QUrl::fromLocalFile(directory.path())); QTRY_COMPARE(sources.gallery().size(), 1);
    sources.selectAll(false); QSignalSpy imported(&sources, &EditorCameraPhoto::importReady);
    QVERIFY(sources.importSelection()); QTRY_COMPARE(imported.count(), 1);
    QCOMPARE(imported[0][0].toUrl(), QUrl::fromLocalFile(path)); QVERIFY(!QFileInfo::exists(root));
}
void CameraPhotoTests::outputStorageFailureIsLocalAndRetryable_data() {
    QTest::addColumn<QString>("operation");
    QTest::newRow("screen-capture") << QString("capture");
    QTest::newRow("native-thumbnail") << QString("thumbnail");
    QTest::newRow("native-original") << QString("original");
}
void CameraPhotoTests::outputStorageFailureIsLocalAndRetryable() {
    QFETCH(QString, operation);
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/photo-storage-failure-XXXXXX"); QVERIFY(directory.isValid());
    const auto previous = qgetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE");
    const auto restore = qScopeGuard([&] { if (previous.isNull()) qunsetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); else qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", previous); });
    const auto root = directory.filePath("blocked"); qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", root.toUtf8());
    QFile blocker(root); QVERIFY(blocker.open(QIODevice::WriteOnly)); QCOMPARE(blocker.write("retained"), qint64(8)); blocker.close();
    auto library = std::make_shared<PhotoFixture>(); QImage image(40, 30, QImage::Format_ARGB32); image.fill(Qt::red);
    EditorCameraPhoto sources(library, [image](const QVariantMap &, CaptureImageReply reply) { reply(image, {}); }, nullptr);
    QSignalSpy imported(&sources, &EditorCameraPhoto::importReady);
    if (operation == "capture") {
        QVERIFY(sources.captureScreen({})); QTRY_VERIFY(!sources.busy());
        QVERIFY(!sources.error().isEmpty()); QVERIFY(sources.currentCapture().isEmpty());
    } else {
        sources.loadGallery("iCloud Photo"); QTRY_COMPARE(sources.gallery().size(), 3);
        if (operation == "thumbnail") sources.requestThumbnail("0");
        else { sources.setSelected("0", false); QVERIFY(sources.importSelection()); QTRY_VERIFY(!sources.galleryBusy()); }
        QVERIFY(!sources.galleryStatus().isEmpty()); QCOMPARE(library->previews, 0); QCOMPARE(library->exports, 0);
        QVERIFY(sources.gallery().last().toMap()["thumbnail"].toUrl().isEmpty());
    }
    QCOMPARE(imported.count(), 0); QVERIFY(blocker.open(QIODevice::ReadOnly)); QCOMPARE(blocker.readAll(), QByteArray("retained")); blocker.close();
    QVERIFY(blocker.remove());
    if (operation == "capture") {
        QVERIFY(sources.captureScreen({})); QTRY_VERIFY(!sources.busy()); QVERIFY(sources.error().isEmpty());
        QCOMPARE(QImage(sources.currentCapture().toLocalFile()), image);
    } else if (operation == "thumbnail") {
        sources.requestThumbnail("0"); QTRY_VERIFY(!sources.gallery().last().toMap()["thumbnail"].toUrl().isEmpty());
        QCOMPARE(library->previews, 1); QVERIFY(sources.galleryStatus().isEmpty());
    } else {
        QVERIFY(sources.importSelection()); QTRY_COMPARE(imported.count(), 1); QCOMPARE(library->exports, 1);
        QCOMPARE(QImage(imported[0][0].toUrl().toLocalFile()).size(), QSize(160, 80)); QVERIFY(sources.galleryStatus().isEmpty());
    }
}
void CameraPhotoTests::repeatedStorageFailureDoesNotReenterGalleryNotifications() {
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/photo-failure-notify-XXXXXX"); QVERIFY(directory.isValid());
    const auto previous = qgetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE");
    const auto restore = qScopeGuard([&] { if (previous.isNull()) qunsetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); else qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", previous); });
    const auto root = directory.filePath("blocked"); qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", root.toUtf8());
    QFile blocker(root); QVERIFY(blocker.open(QIODevice::WriteOnly)); QCOMPARE(blocker.write("retained"), qint64(8)); blocker.close();
    auto library = std::make_shared<PhotoFixture>(); EditorCameraPhoto sources(library, nullptr);
    sources.loadGallery("iCloud Photo"); QTRY_COMPARE(sources.gallery().size(), 3);
    int notifications = 0;
    const auto connection = connect(&sources, &EditorCameraPhoto::galleryChanged, &sources, [&] {
        if (++notifications < 6) sources.requestThumbnail("0");
    });
    sources.requestThumbnail("0"); disconnect(connection);
    QCOMPARE(notifications, 1); QCOMPARE(library->previews, 0); QVERIFY(!sources.galleryStatus().isEmpty());
    QVERIFY(blocker.open(QIODevice::ReadOnly)); QCOMPARE(blocker.readAll(), QByteArray("retained"));
}
void CameraPhotoTests::cancelledCaptureDoesNotAllocateStorage() {
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/photo-cancelled-output-XXXXXX"); QVERIFY(directory.isValid());
    const auto previous = qgetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE");
    const auto restore = qScopeGuard([&] { if (previous.isNull()) qunsetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); else qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", previous); });
    const auto root = directory.filePath("output"); qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", root.toUtf8());
    CaptureImageReply reply;
    EditorCameraPhoto sources(std::make_shared<PhotoFixture>(), [&](const QVariantMap &, CaptureImageReply result) { reply = std::move(result); }, nullptr);
    QVERIFY(sources.captureScreen({})); QVERIFY(reply); sources.cancel();
    QImage image(20, 20, QImage::Format_ARGB32); image.fill(Qt::red); reply(image, {}); QTest::qWait(20);
    QVERIFY(sources.currentCapture().isEmpty()); QVERIFY(!QFileInfo::exists(root));
}
void CameraPhotoTests::transformsFullResolutionPixels() {
    QImage source(160, 90, QImage::Format_ARGB32); source.fill(Qt::red);
    source.setPixelColor(35, 0, Qt::blue);
    const auto cropped = EditorCameraPhoto::frameForSettings(source, {{"cameraAspect", "1:1"}});
    QCOMPARE(cropped.size(), QSize(90, 90)); QCOMPARE(cropped.pixelColor(0, 0), QColor(Qt::blue));
    const auto rotated = EditorCameraPhoto::frameForSettings(source, {{"cameraAspect", "Original"}, {"cameraAngle", "90°"}, {"cameraMirror", true}});
    QCOMPARE(rotated.size(), QSize(90, 160));
    // Mirror is a preview-only control; captured source pixels remain original.
    QCOMPARE(rotated.pixelColor(89, 35), QColor(Qt::blue));
    QCOMPARE(EditorCameraPhoto::frameForSettings(source, {{"cameraAspect", "invalid"}, {"cameraAngle", "bad"}}), source);
    QVERIFY(EditorCameraPhoto::frameForSettings({}, {}).isNull());
}
void CameraPhotoTests::galleryUsesOriginalsAndSelectionScope() {
    EditorCameraPhoto sources(std::make_shared<PhotoFixture>(), nullptr);
    sources.loadGallery("iCloud Photo"); QTRY_VERIFY(!sources.galleryBusy());
    QCOMPARE(sources.gallery().size(), 3); QCOMPARE(sources.gallery().first().toMap()["label"].toString(), QString("Capture.png"));
    sources.requestThumbnail("0");
    QTRY_VERIFY(!sources.gallery().last().toMap()["thumbnail"].toUrl().isEmpty());
    sources.setSelected("0", false); sources.setSelected("1", false); QCOMPARE(sources.selectionCount(), 1);
    sources.setSelected("2", true); QCOMPARE(sources.selectionCount(), 2);
    sources.retainSingleSelection(); QCOMPARE(sources.selectionCount(), 1);
    sources.filterGallery("Coast", "Name"); sources.selectAll(true); QCOMPARE(sources.selectionCount(), 1);
    QSignalSpy imported(&sources, &EditorCameraPhoto::importReady);
    QVERIFY(sources.importSelection()); QTRY_COMPARE(imported.size(), 1);
    QImage original(imported[0][0].toUrl().toLocalFile()); QCOMPARE(original.size(), QSize(160, 80));
    QCOMPARE(QFileInfo(imported[0][0].toUrl().toLocalFile()).fileName(), QString("Coast.png"));
    sources.filterGallery("", "Oldest"); QCOMPARE(sources.gallery().first().toMap()["id"].toString(), QString("0"));
    sources.filterGallery("", "Size"); QCOMPARE(sources.gallery().first().toMap()["id"].toString(), QString("2"));
    sources.selectAll(true); QCOMPARE(sources.selectionCount(), 3);
}
void CameraPhotoTests::nativeImageDecoderRetainsFullResolution() {
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/native-photo-XXXXXX"); QVERIFY(directory.isValid());
    QImage source(512, 320, QImage::Format_ARGB32); source.fill(Qt::blue);
    const auto path = directory.filePath("original.png"); QVERIFY(source.save(path));
    QCOMPARE(editorDecodeOriginalPhoto(path).size(), source.size());
    QVERIFY(editorDecodeOriginalPhoto(directory.filePath("missing.heic")).isNull());
}
void CameraPhotoTests::deniedLibraryDoesNotInventPhotos() {
    auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::Denied;
    EditorCameraPhoto sources(fixture, nullptr); sources.loadGallery("iCloud Photo");
    QVERIFY(!sources.galleryBusy()); QVERIFY(sources.gallery().isEmpty()); QVERIFY(!sources.galleryStatus().isEmpty());
    QVERIFY(!sources.importSelection());
}
void CameraPhotoTests::galleryPermissionWaitIsExplicitAndClearsAcquisitionErrors() {
    auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined;
    fixture->deferAuthorization = true;
    EditorCameraPhoto sources(fixture, nullptr);
    sources.reportImportFailure("Screen Recording is unavailable");
    sources.loadGallery("iCloud Photo");
    QVERIFY(sources.error().isEmpty());
    QVERIFY(!sources.galleryBusy()); QVERIFY(sources.gallery().isEmpty());
    QVERIFY(sources.galleryAuthorizationRequired()); QCOMPARE(fixture->authorizationRequests, 0);
    sources.requestGalleryAccess();
    QCOMPARE(fixture->authorizationRequests, 1); QVERIFY(!sources.galleryBusy());
    sources.requestGalleryAccess(); QCOMPARE(fixture->authorizationRequests, 1);
    fixture->permission = iiPhotoLibrary::Access::Full; fixture->authorizationReply();
    QTRY_COMPARE(sources.gallery().size(), 3); QVERIFY(!sources.galleryAuthorizationRequired());
}
void CameraPhotoTests::galleryAuthorizationDoesNotDispatchForAReplacedView() {
    QTemporaryDir folder(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/authorization-local-XXXXXX"); QVERIFY(folder.isValid());
    auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined; fixture->deferAuthorization = true;
    EditorCameraPhoto source(fixture, nullptr); source.loadGallery("iCloud Photo");
    bool switched = false; connect(&source, &EditorCameraPhoto::galleryChanged, this, [&] {
        if (!switched && source.galleryStatus().contains("Waiting for Photos")) { switched = true; source.loadGallery("Local folder", QUrl::fromLocalFile(folder.path())); }
    });
    source.requestGalleryAccess(); QVERIFY(switched); QCOMPARE(fixture->authorizationRequests, 0);
    QTRY_VERIFY(!source.galleryBusy()); QVERIFY(source.gallery().isEmpty()); QVERIFY(!source.galleryStatus().contains("Waiting for Photos"));
}
void CameraPhotoTests::galleryAuthorizationCompletionUsesTheCurrentPhotosView_data() {
    QTest::addColumn<int>("access");
    QTest::newRow("full") << int(iiPhotoLibrary::Access::Full); QTest::newRow("limited") << int(iiPhotoLibrary::Access::Limited);
    QTest::newRow("denied") << int(iiPhotoLibrary::Access::Denied); QTest::newRow("unsupported") << int(iiPhotoLibrary::Access::Unsupported);
}
void CameraPhotoTests::galleryAuthorizationCompletionUsesTheCurrentPhotosView() {
    QFETCH(int, access); QTemporaryDir folder(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/authorization-return-XXXXXX"); QVERIFY(folder.isValid());
    auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined; fixture->deferAuthorization = true;
    EditorCameraPhoto source(fixture, nullptr); source.loadGallery("iCloud Photo"); source.requestGalleryAccess(); QCOMPARE(fixture->authorizationRequests, 1);
    const auto reply = fixture->authorizationReply; source.loadGallery("Local folder", QUrl::fromLocalFile(folder.path())); QTRY_VERIFY(!source.galleryBusy());
    source.loadGallery("iCloud Photo"); fixture->permission = static_cast<iiPhotoLibrary::Access>(access); reply(); QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall);
    QVERIFY(!source.galleryStatus().contains("Waiting for Photos"));
    if (fixture->permission == iiPhotoLibrary::Access::Full || fixture->permission == iiPhotoLibrary::Access::Limited) { QTRY_COMPARE(source.gallery().size(), 3); QCOMPARE(fixture->scans, 1); }
    else { QVERIFY(source.gallery().isEmpty()); QCOMPARE(fixture->scans, 0); QVERIFY(source.galleryStatus().contains(fixture->permission == iiPhotoLibrary::Access::Unsupported ? "unavailable" : "System Settings")); }
    QVERIFY(!source.galleryBusy()); QVERIFY(!source.galleryAuthorizationRequired()); QCOMPARE(fixture->authorizationRequests, 1);
}
void CameraPhotoTests::galleryAuthorizationCompletionPreservesALocalCatalog() {
    QTemporaryDir folder(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/authorization-stay-local-XXXXXX"); QVERIFY(folder.isValid());
    QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::green); QVERIFY(image.save(folder.filePath("Local.png")));
    auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined; fixture->deferAuthorization = true;
    EditorCameraPhoto source(fixture, nullptr); source.loadGallery("iCloud Photo"); source.requestGalleryAccess(); const auto reply = fixture->authorizationReply;
    source.loadGallery("Local folder", QUrl::fromLocalFile(folder.path())); QTRY_COMPARE(source.gallery().size(), 1); QTRY_VERIFY(!source.galleryBusy());
    const auto before = source.gallery(); const auto status = source.galleryStatus(); auto *model = qobject_cast<QAbstractItemModel *>(source.property("galleryModel").value<QObject *>()); QVERIFY(model); QSignalSpy resets(model, &QAbstractItemModel::modelReset); QVERIFY(resets.isValid());
    fixture->permission = iiPhotoLibrary::Access::Full; reply(); QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall);
    QCOMPARE(source.gallery(), before); QCOMPARE(source.galleryStatus(), status); QCOMPARE(resets.count(), 0); QCOMPARE(fixture->scans, 0); QCOMPARE(fixture->authorizationRequests, 1);
}
void CameraPhotoTests::galleryAuthorizationOldReplyCannotRetireANewerRequest() {
    auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined; fixture->deferAuthorization = true;
    EditorCameraPhoto source(fixture, nullptr); source.loadGallery("iCloud Photo"); source.requestGalleryAccess(); const auto old = fixture->authorizationReply;
    old(); QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall); QVERIFY(source.galleryAuthorizationRequired());
    source.requestGalleryAccess(); const auto current = fixture->authorizationReply; QCOMPARE(fixture->authorizationRequests, 2); old(); QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall);
    QVERIFY(!source.galleryAuthorizationRequired()); source.requestGalleryAccess(); QCOMPARE(fixture->authorizationRequests, 2);
    fixture->permission = iiPhotoLibrary::Access::Full; current(); QTRY_COMPARE(source.gallery().size(), 3); QCOMPARE(fixture->scans, 1);
}
void CameraPhotoTests::galleryAuthorizationReplyIsConsumedOnce() {
    auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined; fixture->deferAuthorization = true;
    EditorCameraPhoto source(fixture, nullptr); source.loadGallery("iCloud Photo"); source.requestGalleryAccess(); const auto reply = fixture->authorizationReply;
    fixture->permission = iiPhotoLibrary::Access::Full; reply(); QTRY_COMPARE(source.gallery().size(), 3); QSignalSpy notifications(&source, &EditorCameraPhoto::galleryChanged);
    reply(); reply(); QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall); QCOMPARE(notifications.count(), 0); QCOMPARE(fixture->scans, 1);
}
void CameraPhotoTests::galleryAuthorizationRechecksAccessBeforeDispatch_data() {
    QTest::addColumn<int>("access"); QTest::newRow("already-granted") << int(iiPhotoLibrary::Access::Full); QTest::newRow("already-denied") << int(iiPhotoLibrary::Access::Denied);
}
void CameraPhotoTests::galleryAuthorizationRechecksAccessBeforeDispatch() {
    QFETCH(int, access); auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined; fixture->deferAuthorization = true;
    EditorCameraPhoto source(fixture, nullptr); source.loadGallery("iCloud Photo"); connect(&source, &EditorCameraPhoto::galleryChanged, this, [&] { if (source.galleryStatus().contains("Waiting for Photos")) fixture->permission = static_cast<iiPhotoLibrary::Access>(access); });
    source.requestGalleryAccess(); QCOMPARE(fixture->authorizationRequests, 0); QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall);
    QVERIFY(!source.galleryStatus().contains("Waiting for Photos"));
    if (fixture->permission == iiPhotoLibrary::Access::Full) QTRY_COMPARE(source.gallery().size(), 3); else QVERIFY(source.galleryStatus().contains("System Settings"));
}
void CameraPhotoTests::galleryAuthorizationOwnerCanClose_data() {
    QTest::addColumn<bool>("beforeDispatch"); QTest::newRow("pending-observer") << true; QTest::newRow("pending-provider") << false;
}
void CameraPhotoTests::galleryAuthorizationOwnerCanClose() {
    QFETCH(bool, beforeDispatch); auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined; fixture->deferAuthorization = true;
    auto source = std::make_unique<EditorCameraPhoto>(fixture, nullptr); source->loadGallery("iCloud Photo"); QPointer<QObject> observed(source.get());
    if (beforeDispatch) connect(source.get(), &EditorCameraPhoto::galleryChanged, this, [&] { if (source->galleryStatus().contains("Waiting for Photos")) source.reset(); });
    source->requestGalleryAccess();
    if (beforeDispatch) QCOMPARE(fixture->authorizationRequests, 0);
    else { QCOMPARE(fixture->authorizationRequests, 1); const auto reply = fixture->authorizationReply; source.reset(); fixture->permission = iiPhotoLibrary::Access::Full; reply(); QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall); }
    QVERIFY(!source); QVERIFY(observed.isNull()); QCOMPARE(fixture->scans, 0);
}
void CameraPhotoTests::galleryAuthorizationFailureCanRetry_data() {
    QTest::addColumn<bool>("replyFirst"); QTest::newRow("before-reply") << false; QTest::newRow("after-queued-reply") << true;
}
void CameraPhotoTests::galleryAuthorizationFailureCanRetry() {
    QFETCH(bool, replyFirst); auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined;
    fixture->authorization = [replyFirst](std::function<void()> done) { if (replyFirst) done(); throw std::runtime_error("controlled permission failure"); };
    EditorCameraPhoto source(fixture, nullptr); source.loadGallery("iCloud Photo"); source.requestGalleryAccess();
    QVERIFY(source.galleryAuthorizationRequired()); QVERIFY(source.galleryStatus().contains("controlled permission failure")); QVERIFY(source.error().isEmpty()); QCOMPARE(fixture->authorizationRequests, 1);
    fixture->authorization = {}; fixture->deferAuthorization = true; source.requestGalleryAccess(); const auto reply = fixture->authorizationReply;
    QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall); QVERIFY(!source.galleryAuthorizationRequired()); QCOMPARE(fixture->authorizationRequests, 2);
    fixture->permission = iiPhotoLibrary::Access::Full; reply(); QTRY_COMPARE(source.gallery().size(), 3); QCOMPARE(fixture->scans, 1);
}
void CameraPhotoTests::galleryAuthorizationFailurePreservesANewerLocalView() {
    QTemporaryDir folder(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/authorization-failed-local-XXXXXX"); QVERIFY(folder.isValid());
    auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined; EditorCameraPhoto source(fixture, nullptr); source.loadGallery("iCloud Photo");
    fixture->authorization = [&](std::function<void()>) { source.loadGallery("Local folder", QUrl::fromLocalFile(folder.path())); throw std::runtime_error("controlled permission failure"); };
    source.requestGalleryAccess(); QTRY_VERIFY(!source.galleryBusy()); QVERIFY(!source.galleryStatus().contains("controlled permission failure")); QVERIFY(source.gallery().isEmpty()); QCOMPARE(fixture->scans, 0);
}
void CameraPhotoTests::galleryAuthorizationCompletionReturnsToTheOwnerThread() {
    auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined; fixture->deferAuthorization = true;
    EditorCameraPhoto source(fixture, nullptr); source.loadGallery("iCloud Photo"); const auto owner = std::this_thread::get_id(); bool onOwner = true;
    connect(&source, &EditorCameraPhoto::galleryChanged, this, [&] { onOwner &= std::this_thread::get_id() == owner; }, Qt::DirectConnection);
    source.requestGalleryAccess(); const auto reply = fixture->authorizationReply; fixture->permission = iiPhotoLibrary::Access::Full;
    std::jthread worker([reply] { reply(); }); worker.join(); QTRY_COMPARE(source.gallery().size(), 3); QVERIFY(onOwner); QCOMPARE(fixture->scans, 1);
}
void CameraPhotoTests::galleryAuthorizationCanCompleteSynchronously() {
    auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined;
    fixture->authorization = [library = fixture.get()](std::function<void()> done) { library->permission = iiPhotoLibrary::Access::Full; done(); };
    EditorCameraPhoto source(fixture, nullptr); source.loadGallery("iCloud Photo"); source.requestGalleryAccess(); QCOMPARE(fixture->authorizationRequests, 1);
    QTRY_COMPARE(source.gallery().size(), 3); QCOMPARE(fixture->scans, 1); QVERIFY(!source.galleryStatus().contains("Waiting for Photos"));
}
void CameraPhotoTests::galleryAuthorizationSurvivesARefreshOfTheSamePhotosView() {
    auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined; fixture->deferAuthorization = true;
    EditorCameraPhoto source(fixture, nullptr); source.loadGallery("iCloud Photo"); bool refreshed = false;
    connect(&source, &EditorCameraPhoto::galleryChanged, this, [&] { if (!refreshed && source.galleryStatus().contains("Waiting for Photos")) { refreshed = true; source.loadGallery("iCloud Photo"); } });
    source.requestGalleryAccess(); QVERIFY(refreshed); QCOMPARE(fixture->authorizationRequests, 1); fixture->permission = iiPhotoLibrary::Access::Full; fixture->authorizationReply();
    QTRY_COMPARE(source.gallery().size(), 3); QCOMPARE(fixture->scans, 1);
}
void CameraPhotoTests::galleryPresenterEndDefersAuthorizedReads_data() {
    QTest::addColumn<bool>("destroy"); QTest::newRow("detach") << false; QTest::newRow("destroy-presenter") << true;
}
void CameraPhotoTests::galleryPresenterEndDefersAuthorizedReads() {
    QFETCH(bool, destroy); auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined; fixture->deferAuthorization = true;
    EditorCameraPhoto source(fixture, nullptr); auto presenter = std::make_unique<QObject>(); source.attachGallery(presenter.get()); source.loadGallery("iCloud Photo"); source.requestGalleryAccess();
    const auto reply = fixture->authorizationReply; if (destroy) presenter.reset(); else QVERIFY(source.detachGallery(presenter.get()));
    fixture->permission = iiPhotoLibrary::Access::Full; reply(); QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall);
    QVERIFY(!source.galleryBusy()); QVERIFY(!source.galleryAuthorizationRequired()); QVERIFY(source.galleryStatus().isEmpty()); QCOMPARE(fixture->scans, 0);
    QObject returned; source.attachGallery(&returned); source.loadGallery("iCloud Photo"); QTRY_COMPARE(source.gallery().size(), 3); QCOMPARE(fixture->scans, 1); QCOMPARE(fixture->authorizationRequests, 1);
}
void CameraPhotoTests::galleryPresenterRetirementBeforeDispatchSkipsAuthorization() {
    auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined; fixture->deferAuthorization = true;
    EditorCameraPhoto source(fixture, nullptr); QObject presenter; source.attachGallery(&presenter); source.loadGallery("iCloud Photo"); bool detached = false;
    connect(&source, &EditorCameraPhoto::galleryChanged, this, [&] { if (!detached && source.galleryStatus().contains("Waiting for Photos")) { detached = true; QVERIFY(source.detachGallery(&presenter)); } });
    source.requestGalleryAccess(); QVERIFY(detached); QCOMPARE(fixture->authorizationRequests, 0); QVERIFY(!source.galleryAuthorizationRequired()); QVERIFY(source.galleryStatus().isEmpty());
}
void CameraPhotoTests::staleGalleryPresenterCannotRetireTheCurrentView() {
    auto fixture = std::make_shared<PhotoFixture>(); fixture->permission = iiPhotoLibrary::Access::NotDetermined; fixture->deferAuthorization = true;
    EditorCameraPhoto source(fixture, nullptr); auto old = std::make_unique<QObject>(); QObject current; source.attachGallery(old.get()); source.loadGallery("iCloud Photo"); source.requestGalleryAccess();
    source.attachGallery(&current); QVERIFY(!source.detachGallery(old.get())); old.reset(); fixture->permission = iiPhotoLibrary::Access::Full; fixture->authorizationReply();
    QTRY_COMPARE(source.gallery().size(), 3); QCOMPARE(fixture->scans, 1); QVERIFY(source.detachGallery(&current)); QVERIFY(!source.detachGallery(&current));
}
void CameraPhotoTests::retiredGalleryDoesNotAcquireAThumbnail() {
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/retired-gallery-XXXXXX"); QVERIFY(directory.isValid());
    const auto previous = qgetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); const auto restore = qScopeGuard([&] { if (previous.isNull()) qunsetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); else qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", previous); });
    const auto output = directory.filePath("unused-output"); qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", output.toUtf8());
    auto fixture = std::make_shared<PhotoFixture>(); EditorCameraPhoto source(fixture, nullptr); QObject presenter; source.attachGallery(&presenter); source.loadGallery("iCloud Photo"); QTRY_COMPARE(source.gallery().size(), 3);
    QVERIFY(source.detachGallery(&presenter)); const auto before = source.gallery(); source.requestThumbnail("0"); QCOMPARE(source.gallery(), before); QCOMPARE(fixture->previews, 0); QVERIFY(!QFileInfo::exists(output));
}
void CameraPhotoTests::reattachedGalleryCanAcquireFromItsCachedCatalog() {
    auto fixture = std::make_shared<PhotoFixture>(); EditorCameraPhoto source(fixture, nullptr); QObject first, second;
    source.attachGallery(&first); source.loadGallery("iCloud Photo"); QTRY_COMPARE(source.gallery().size(), 3);
    QVERIFY(source.detachGallery(&first)); source.attachGallery(&second); source.requestThumbnail("0");
    QTRY_VERIFY(!source.gallery().last().toMap()["thumbnail"].toUrl().isEmpty()); QCOMPARE(fixture->previews, 1);
    const QImage thumbnail(source.gallery().last().toMap()["thumbnail"].toUrl().toLocalFile()); QVERIFY(!thumbnail.isNull()); QCOMPARE(thumbnail.size(), QSize(20, 10));
    const auto color = thumbnail.pixelColor(0, 0); QVERIFY(color.red() <= 2 && color.green() <= 2 && color.blue() >= 253); QCOMPARE(fixture->scans, 1);
}
void CameraPhotoTests::captureWaitsForImportAndCancelsOldCallbacks() {
    QList<CaptureImageReply> pending;
    EditorCameraPhoto sources(std::make_shared<PhotoFixture>(), [&](const QVariantMap &, CaptureImageReply reply) { pending.append(reply); }, nullptr);
    QSignalSpy imported(&sources, &EditorCameraPhoto::importReady);
    QVERIFY(sources.captureScreen({})); QVERIFY(sources.busy()); QCOMPARE(pending.size(), 1);
    sources.cancel(); QVERIFY(sources.captureScreen({})); QCOMPARE(pending.size(), 2);
    QImage obsolete(4, 4, QImage::Format_ARGB32); obsolete.fill(Qt::red); pending[0](obsolete, {});
    QTest::qWait(20); QVERIFY(sources.currentCapture().isEmpty()); QVERIFY(sources.busy());
    QImage image(120, 80, QImage::Format_ARGB32); image.fill(Qt::green); pending[1](image, {});
    QTRY_VERIFY(!sources.busy()); QVERIFY(!sources.currentCapture().isEmpty()); QCOMPARE(imported.size(), 0);
    QCOMPARE(QImage(sources.currentCapture().toLocalFile()), image);
    QVERIFY(sources.importCurrentCapture()); QCOMPARE(imported.size(), 1);
}
void CameraPhotoTests::timerCancellationPreventsCapture() {
    int captures = 0;
    EditorCameraPhoto sources(std::make_shared<PhotoFixture>(), [&](const QVariantMap &, CaptureImageReply) { ++captures; }, nullptr);
    QVERIFY(sources.captureScreen({{"captureDelay", 1}})); QCOMPARE(sources.countdown(), 1);
    sources.cancel(); QTest::qWait(1100); QCOMPARE(captures, 0); QVERIFY(!sources.busy()); QCOMPARE(sources.countdown(), 0);
}
QTEST_MAIN(CameraPhotoTests)
#include "tst_cameraphoto.moc"

