#include "App/Views/Editor/EditorCameraPhoto.h"
#include "App/Views/Editor/EditorPhotoImageNative.h"
#include <QSignalSpy>
#include <QImageReader>
#include <QFileInfo>
#include <QDir>
#include <QtTest>

namespace {
class PhotoFixture final : public iiPhotoLibrary::PhotoLibrary {
public:
    iiPhotoLibrary::Access permission = iiPhotoLibrary::Access::Full;
    int authorizationRequests = 0;
    bool deferAuthorization = false;
    std::function<void()> authorizationReply;
    QString name() const override { return "Fixture"; }
    iiPhotoLibrary::Access access() const override { return permission; }
    void authorize(std::function<void()> done) override {
        ++authorizationRequests;
        if (deferAuthorization) authorizationReply = std::move(done);
        else done();
    }
    iiPhotoLibrary::Snapshot scan(QString *) override {
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
        QImage image(20, 10, QImage::Format_ARGB32); image.fill(Qt::blue); return image.save(path);
    }
    bool exportResource(const QString &, const QString &token, const QString &path, QString *) override {
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
    void transformsFullResolutionPixels();
    void nativeImageDecoderRetainsFullResolution();
    void galleryUsesOriginalsAndSelectionScope();
    void deniedLibraryDoesNotInventPhotos();
    void galleryPermissionWaitIsExplicitAndClearsAcquisitionErrors();
    void captureWaitsForImportAndCancelsOldCallbacks();
    void timerCancellationPreventsCapture();
};
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

