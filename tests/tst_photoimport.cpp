#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/Editor/EditorCameraPhoto.h"
#include "App/Views/Editor/EditorPhotoImportCommand.h"
#include <SocietyDrive.h>
#include <iiFileProviderNative.h>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QProcess>
#include <QtConcurrentRun>
#include <QMediaDevices>
#include <QThreadPool>
#include <QScopeGuard>
#include <QTimer>
#include <QtTest>
#include <atomic>
#include <semaphore>
#include <mutex>
#include <thread>
using namespace iiSharedCanvas;
namespace {
Document picture(std::string name = "First") {
    Document d; d.extent = {8, 8};
    d.assets.emplace_back(RasterAsset{"pixels", makeRasterLayer(8, 8, 0xff112233U)});
    d.layers.emplace_back(StaticBitmapLayer{{"layer", std::move(name)}, StaticSource{"pixels"}}); return d;
}
struct Gate {
    std::atomic_bool started = false, finished = false;
    std::atomic_int calls = 0, active = 0, maximum = 0;
    std::atomic_bool worker = false, sourceAlive = false;
    std::counting_semaphore<8> release{0};
    std::mutex mutex;
    std::filesystem::path source;
};
struct Release { std::shared_ptr<Gate> gate; ~Release() { gate->release.release(); } };
dreamscapes::PhotoImportOperation gated(const std::shared_ptr<Gate> &gate) {
    const auto owner = std::this_thread::get_id();
    return [gate, owner](const dreamscapes::PhotoImportInput &input) {
        gate->worker = std::this_thread::get_id() != owner;
        const auto active = ++gate->active; gate->maximum = std::max(gate->maximum.load(), active);
        { std::lock_guard lock(gate->mutex); gate->source = input.originals.paths.front(); }
        if (++gate->calls == 1) { gate->started = true; (void)gate->release.try_acquire_for(std::chrono::seconds(8)); }
        gate->sourceAlive = std::filesystem::is_regular_file(input.originals.paths.front());
        auto result = dreamscapes::preparePhotoImport(input); --gate->active; gate->finished = true; return result;
    };
}
class BlockedLibrary final : public iiPhotoLibrary::PhotoLibrary {
public:
    std::shared_ptr<Gate> gate;
    std::atomic_int previews = 0;
    std::atomic_bool failScan = false;
    explicit BlockedLibrary(std::shared_ptr<Gate> value) : gate(std::move(value)) {}
    QString name() const override { return "Photo import fixture"; }
    iiPhotoLibrary::Access access() const override { return iiPhotoLibrary::Access::Full; }
    void authorize(std::function<void()> done) override { done(); }
    iiPhotoLibrary::Snapshot scan(QString *) override {
        if (failScan.exchange(false)) throw std::runtime_error("Gallery scan fixture failure");
        iiPhotoLibrary::Asset a; a.identifier = "one"; a.name = "Original.png"; a.media = "photo";
        a.resources.append({"original", "Original.png", "photo"});
        iiPhotoLibrary::Snapshot result; result.complete = true; result.assets.append(a); return result;
    }
    bool preview(const QString &, const QString &, QString *) override { ++previews; return false; }
    bool exportResource(const QString &, const QString &, const QString &path, QString *) override {
        gate->started = true; (void)gate->release.try_acquire_for(std::chrono::seconds(8));
        QImage image(32, 16, QImage::Format_ARGB32); image.fill(Qt::green);
        const bool saved = image.save(path); gate->finished = true; return saved;
    }
    QString import(const QJsonObject &, const QStringList &, QString *) override { return {}; }
    bool trash(const QString &, QString *) override { return false; }
};
class PhotoQueueFixture final : public iiPhotoLibrary::PhotoLibrary {
public:
    const std::shared_ptr<Gate> gate = std::make_shared<Gate>();
    const std::thread::id owner = std::this_thread::get_id();
    std::atomic_int scans = 0, previews = 0, exports = 0;
    std::atomic_bool allWorker = true, timedOut = false;
    bool blockScan = false, blockPreview = false, blockExport = false;
    std::mutex mutex;
    QStringList calls;
    void enter(const QString &name) {
        if (std::this_thread::get_id() == owner) allWorker = false;
        const int active = ++gate->active; gate->maximum = std::max(gate->maximum.load(), active);
        std::lock_guard lock(mutex); calls.append(name);
    }
    QStringList order() { std::lock_guard lock(mutex); return calls; }
    void block() { gate->started = true; if (!gate->release.try_acquire_for(std::chrono::seconds(8))) timedOut = true; }
    QString name() const override { return "Queued photo fixture"; }
    iiPhotoLibrary::Access access() const override { return iiPhotoLibrary::Access::Full; }
    void authorize(std::function<void()> done) override { done(); }
    iiPhotoLibrary::Snapshot scan(QString *) override {
        const int count = ++scans; enter(QString("scan:%1").arg(count));
        const auto leave = qScopeGuard([&] { --gate->active; });
        if (blockScan && count == 1) block();
        iiPhotoLibrary::Snapshot result; result.complete = true;
        for (int i = 0; i < 3; ++i) {
            iiPhotoLibrary::Asset asset; asset.identifier = QString::number(i); asset.name = QString("Photo%1.png").arg(i); asset.media = "photo";
            asset.resources.append({"original", asset.name, "photo"}); result.assets.append(asset);
        }
        return result;
    }
    bool preview(const QString &id, const QString &path, QString *) override {
        const int count = ++previews; enter("preview:" + id); const auto leave = qScopeGuard([&] { --gate->active; });
        if (blockPreview && count == 1) block();
        QImage image(4, 4, QImage::Format_ARGB32); image.fill(Qt::blue); return image.save(path);
    }
    bool exportResource(const QString &id, const QString &, const QString &path, QString *) override {
        const int count = ++exports; enter("original:" + id); const auto leave = qScopeGuard([&] { --gate->active; });
        if (blockExport && count == 1) block();
        QImage image(4, 4, QImage::Format_ARGB32); image.fill(Qt::green); return image.save(path);
    }
    QString import(const QJsonObject &, const QStringList &, QString *) override { return {}; }
    bool trash(const QString &, QString *) override { return false; }
};
}
class PhotoImportTests : public QObject {
    Q_OBJECT
    QTemporaryDir storage{DREAMSCAPES_TEST_DIRECTORY "/photo-import-documents-XXXXXX.societycontainer"};
    QByteArray oldContainer;
    QString folder() {
        const auto directory = storage.filePath("sources");
        if (!QDir().mkpath(directory)) return {};
        QImage source(32, 16, QImage::Format_ARGB32); source.fill(Qt::red); source.setPixel(2, 3, qRgb(0, 0, 255));
        return source.save(directory + "/Original.png") ? directory : QString{};
    }
    static EditorCameraPhoto *photos(EditorCanvas &canvas) { return qobject_cast<EditorCameraPhoto *>(canvas.photoSources()); }
    static bool select(EditorCameraPhoto &source, const QString &directory) {
        source.loadGallery("Local folder", QUrl::fromLocalFile(directory));
        QElapsedTimer timer; timer.start();
        while (source.galleryBusy() && timer.elapsed() < 5000) QTest::qWait(1);
        if (source.gallery().isEmpty() || source.galleryBusy()) return false;
        source.setSelected(source.gallery().first().toMap()["id"].toString(), false); return true;
    }
private slots:
    void initTestCase() {
        oldContainer = qgetenv("SOCIETY_CONTAINER_PATH"); QVERIFY(storage.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path())); qputenv("SOCIETY_CONTAINER_PATH", storage.path().toUtf8());
    }
    void cleanupTestCase() { if (oldContainer.isNull()) qunsetenv("SOCIETY_CONTAINER_PATH"); else qputenv("SOCIETY_CONTAINER_PATH", oldContainer); }
    void replacementDocumentDoesNotReceiveAnOldImport() {
        const auto directory = folder(); QVERIFY(!directory.isEmpty()); EditorCanvas canvas;
        QVERIFY(canvas.loadProjectPage(picture(), "First")); auto *source = photos(canvas); QVERIFY(source);
        source->loadGallery("Local folder", QUrl::fromLocalFile(directory)); QTRY_VERIFY(!source->galleryBusy()); QCOMPARE(source->gallery().size(), 1);
        source->setSelected(source->gallery().first().toMap()["id"].toString(), false);
        QVERIFY(source->importSelection()); QVERIFY(canvas.loadProjectPage(picture("Second"), "Second"));
        const auto bytes = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        QTRY_VERIFY(!source->galleryBusy()); QTest::qWait(50);
        QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes); QCOMPARE(canvas.revision(), revision); QCOMPARE(canvas.document()->layers.size(), std::size_t(1));
    }
    void sameTargetReceivesOriginalPixels() {
        const auto directory = folder(); QVERIFY(!directory.isEmpty()); EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Same"));
        auto *source = photos(canvas); source->loadGallery("Local folder", QUrl::fromLocalFile(directory)); QTRY_VERIFY(!source->galleryBusy());
        source->setSelected(source->gallery().first().toMap()["id"].toString(), false); QSignalSpy imported(source, &EditorCameraPhoto::importReady);
        QVERIFY(source->importSelection()); QTRY_COMPARE(imported.count(), 1); QTRY_COMPARE(canvas.document()->layers.size(), std::size_t(2));
        const auto *asset = resolveAssetAt(*canvas.document(), canvas.document()->layers.back(), 0); QVERIFY(asset);
        const auto *raster = std::get_if<RasterAsset>(asset); QVERIFY(raster); QCOMPARE(raster->pixels.width, 32); QCOMPARE(raster->pixels.height, 16);
        QCOMPARE(raster->pixels.pixels[3 * 32 + 2], 0xff0000ffU);
    }
    void closingAcquisitionDoesNotJoinAnOriginalExport() {
        const auto gate = std::make_shared<Gate>();
        auto source = std::make_unique<EditorCameraPhoto>(std::make_shared<BlockedLibrary>(gate), nullptr);
        source->loadGallery("iCloud Photo"); QTRY_COMPARE(source->gallery().size(), 1); source->setSelected("one", false);
        QVERIFY(source->importSelection()); QTRY_VERIFY(gate->started.load());
        std::jthread release([gate] { std::this_thread::sleep_for(std::chrono::milliseconds(600)); gate->release.release(); });
        QElapsedTimer elapsed; elapsed.start(); source.reset(); const auto microseconds = elapsed.nsecsElapsed() / 1000;
        QVERIFY2(microseconds < 250000, "Closing acquisition must not wait for original export");
        QTRY_VERIFY(gate->finished.load()); qInfo() << "photo_acquisition_close_us" << microseconds;
    }
    void captureStorageUsesAWorkerBeforePublishingItsPath_data() {
        QTest::addColumn<QString>("action");
        for (const auto &action : {"complete", "cancel-before-storage", "close-before-storage"}) QTest::newRow(action) << QString(action);
    }
    void captureStorageUsesAWorkerBeforePublishingItsPath() {
        QFETCH(QString, action); QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/capture-storage-XXXXXX"); QVERIFY(directory.isValid());
        const auto old = qgetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); const auto root = directory.filePath("outputs");
        qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", root.toUtf8());
        const auto restore = qScopeGuard([&] { if (old.isNull()) qunsetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); else qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", old); });
        QImage image(32, 16, QImage::Format_ARGB32); image.fill(Qt::red); image.setPixel(2, 3, 0xff0000ffU);
        auto source = std::make_unique<EditorCameraPhoto>(std::make_shared<BlockedLibrary>(std::make_shared<Gate>()), [image](const QVariantMap &, CaptureImageReply reply) { reply(image, {}); }, nullptr);
        auto gate = std::make_shared<Gate>(); auto *pool = QThreadPool::globalInstance(); QVERIFY(pool->waitForDone(5000)); const int previous = pool->maxThreadCount(); pool->setMaxThreadCount(1);
        struct Restore { std::shared_ptr<Gate> gate; QThreadPool *pool; int maximum; ~Restore() { gate->release.release(); pool->setMaxThreadCount(maximum); } } release{gate, pool, previous};
        auto blocker = QtConcurrent::run([gate] { gate->started = true; (void)gate->release.try_acquire_for(std::chrono::seconds(8)); }); QTRY_VERIFY(gate->started.load());
        QVERIFY(source->captureScreen({})); bool heartbeat = false; QTimer::singleShot(0, source.get(), [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat); QTest::qWait(30);
        QVERIFY2(source->busy(), "Capture image encoding/storage bypassed the blocked worker."); QVERIFY(source->currentCapture().isEmpty()); QVERIFY(!QFileInfo::exists(root));
        if (action == "cancel-before-storage") source->cancel();
        if (action == "close-before-storage") { QElapsedTimer elapsed; elapsed.start(); source.reset(); QVERIFY(elapsed.elapsed() < 250); }
        gate->release.release(); QTRY_VERIFY(blocker.isFinished());
        if (action != "complete") { QVERIFY(pool->waitForDone(5000)); QTest::qWait(30); if (source) { QVERIFY(!source->busy()); QVERIFY(source->currentCapture().isEmpty()); } QVERIFY(!QFileInfo::exists(root)); }
        else { QTRY_VERIFY(!source->busy()); QVERIFY(!source->currentCapture().isEmpty()); QCOMPARE(QImage(source->currentCapture().toLocalFile()), image); }
    }
    void captureAcceptanceObserversCanCancelOrDelete_data() {
        QTest::addColumn<bool>("destroy"); QTest::newRow("cancel") << false; QTest::newRow("delete") << true;
    }
    void captureAcceptanceObserversCanCancelOrDelete() {
        QFETCH(bool, destroy); int captures = 0;
        auto source = std::make_unique<EditorCameraPhoto>(std::make_shared<BlockedLibrary>(std::make_shared<Gate>()), [&](const QVariantMap &, CaptureImageReply reply) { ++captures; reply(QImage(4, 4, QImage::Format_ARGB32), {}); }, nullptr);
        connect(source.get(), &EditorCameraPhoto::changed, this, [&] { if (!source || !source->busy()) return; if (destroy) source.reset(); else source->cancel(); });
        QVERIFY(source->captureScreen({})); QTest::qWait(30); QCOMPARE(captures, 0);
        if (destroy) QVERIFY(!source); else QVERIFY(!source->busy());
    }
    void failedCaptureStorageCanRetry() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/capture-retry-XXXXXX"); QVERIFY(directory.isValid());
        const auto root = directory.filePath("outputs"); QFile blocker(root); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        const auto old = qgetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", root.toUtf8());
        const auto restore = qScopeGuard([&] { if (old.isNull()) qunsetenv("DREAMSCAPES_EDITOR_SOURCE_CACHE"); else qputenv("DREAMSCAPES_EDITOR_SOURCE_CACHE", old); });
        QImage image(4, 4, QImage::Format_ARGB32); image.fill(Qt::green);
        EditorCameraPhoto source(std::make_shared<BlockedLibrary>(std::make_shared<Gate>()), [image](const QVariantMap &, CaptureImageReply reply) { reply(image, {}); }, nullptr);
        QVERIFY(source.captureScreen({})); QTRY_VERIFY(!source.busy()); QVERIFY(!source.error().isEmpty()); QVERIFY(source.currentCapture().isEmpty());
        QVERIFY(QFile::remove(root)); QVERIFY(source.captureScreen({})); QTRY_VERIFY(!source.busy()); QVERIFY(source.error().isEmpty()); QCOMPARE(QImage(source.currentCapture().toLocalFile()), image);
    }
    void preparingDoesNotMutateFilesOrRestartForViewChanges() {
        const auto directory = folder(); QVERIFY(!directory.isEmpty()); auto document = picture();
        document.layers.emplace_back(StaticBitmapLayer{{"second", "Other"}, StaticSource{"pixels"}});
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(document, "View state")); QVERIFY(canvas.selectLayer("layer"));
        const auto path = storage.filePath("Files/view-state.iisc"); QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        const auto native = std::filesystem::path(path.toStdU16String()); const auto saved = iiFileProvider::native::File::read(native);
        const auto bytes = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision(); const auto alias = canvas.fileAlias();
        EditorCameraPhoto source; QVERIFY(select(source, directory)); auto gate = std::make_shared<Gate>(); Release release{gate};
        EditorPhotoImportCommand command(&canvas, &source, gated(gate));
        QSignalSpy revisions(&canvas, &EditorCanvas::revisionChanged), files(&canvas, &EditorCanvas::fileInformationChanged), ready(&source, &EditorCameraPhoto::importReady);
        QVERIFY(source.importSelection()); QTRY_VERIFY(gate->started.load()); QVERIFY(source.importing()); QVERIFY(!source.importSelection());
        bool heartbeat = false; QTimer::singleShot(0, &source, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
        canvas.setZoom(2); canvas.setPanX(4); QVERIFY(canvas.selectLayer("second")); canvas.configureTool("brush", {});
        source.filterGallery("Original", "Name");
        QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes); QCOMPARE(canvas.revision(), revision); QCOMPARE(canvas.fileAlias(), alias);
        QCOMPARE(iiFileProvider::native::File::read(native), saved); QCOMPARE(revisions.count(), 0); QCOMPARE(files.count(), 0);
        gate->release.release(); QTRY_COMPARE(ready.count(), 1); QVERIFY(gate->worker.load()); QCOMPARE(gate->calls.load(), 1);
        QCOMPARE(canvas.selectedLayerId(), QString("second")); QCOMPARE(canvas.revision(), revision + 1); QCOMPARE(revisions.count(), 1);
        EditorCanvas reopened; QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(path))); QCOMPARE(reopened.document()->layers.size(), std::size_t(3));
    }
    void navigationKeepsTheCapturedArtboardDestination() {
        const auto directory = folder(); auto document = picture(); document.extent = {32, 8};
        document.artboards = {{"first", "First", {{0, 0}, {8, 8}}, 0}, {"other", "Other", {{16, 0}, {8, 8}}, 0}};
        layerProperties(document.layers.front()).artboardId = "first";
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(document, "Boards")); QVERIFY(canvas.selectLayer("layer"));
        EditorCameraPhoto source; QVERIFY(select(source, directory)); auto gate = std::make_shared<Gate>(); Release release{gate};
        EditorPhotoImportCommand command(&canvas, &source, gated(gate)); QSignalSpy ready(&source, &EditorCameraPhoto::importReady);
        QVERIFY(source.importSelection()); QTRY_VERIFY(gate->started.load()); QVERIFY(canvas.selectArtboard(1, false));
        gate->release.release(); QTRY_COMPARE(ready.count(), 1); QCOMPARE(canvas.activeArtboardIndex(), 1);
        const auto &added = layerProperties(canvas.document()->layers.back()); QVERIFY(added.artboardId); QCOMPARE(*added.artboardId, std::string("first"));
        QCOMPARE(added.transform.m11, .25); QCOMPARE(added.transform.translationY, 2.0);
    }
    void targetChangesAndCancellationRejectLatePreparedPixels_data() {
        QTest::addColumn<QString>("change");
        for (auto value : {"document", "revision", "cancel", "stroke"}) QTest::newRow(value) << QString(value);
    }
    void targetChangesAndCancellationRejectLatePreparedPixels() {
        QFETCH(QString, change); const auto directory = folder(); EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Old target"));
        EditorCameraPhoto source; QVERIFY(select(source, directory)); auto gate = std::make_shared<Gate>(); Release release{gate};
        EditorPhotoImportCommand command(&canvas, &source, gated(gate)); QSignalSpy ready(&source, &EditorCameraPhoto::importReady);
        QVERIFY(source.importSelection()); QTRY_VERIFY(gate->started.load());
        if (change == "document") QVERIFY(canvas.loadProjectPage(picture("Replacement"), "Replacement"));
        else if (change == "revision") QVERIFY(canvas.setLayerName("layer", "Other edit"));
        else if (change == "stroke") { canvas.configureTool("brush", {}); QVERIFY(canvas.beginStrokeAt({2, 2})); }
        else source.cancelImport();
        QVERIFY(!source.importing()); const auto bytes = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        gate->release.release(); QTRY_VERIFY(gate->finished.load()); QTest::qWait(30);
        QCOMPARE(ready.count(), 0); QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes);
        if (canvas.liveStrokeActive()) canvas.configureTool("brush", {});
    }
    void aCancelledPreparationDoesNotBlockTheNextAcceptedBatch() {
        const auto directory = folder(); EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Retry"));
        EditorCameraPhoto source; QVERIFY(select(source, directory)); auto gate = std::make_shared<Gate>(); Release release{gate};
        EditorPhotoImportCommand command(&canvas, &source, gated(gate)); QSignalSpy ready(&source, &EditorCameraPhoto::importReady);
        QVERIFY(source.importSelection()); QTRY_VERIFY(gate->started.load()); source.cancelImport(); QVERIFY(source.importSelection());
        gate->release.release(); QTRY_COMPARE(ready.count(), 1); QCOMPARE(gate->calls.load(), 2); QCOMPARE(gate->maximum.load(), 1);
        QCOMPARE(canvas.document()->layers.size(), std::size_t(2)); QVERIFY(!source.importing());
    }
    void closingThePlacementOwnerDoesNotJoinAndLeasesTheOriginal() {
        auto exportGate = std::make_shared<Gate>(); exportGate->release.release();
        auto source = std::make_unique<EditorCameraPhoto>(std::make_shared<BlockedLibrary>(exportGate), nullptr);
        source->loadGallery("iCloud Photo"); QTRY_COMPARE(source->gallery().size(), 1); source->setSelected("one", false);
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Lifetime")); auto gate = std::make_shared<Gate>(); Release release{gate};
        auto command = std::make_unique<EditorPhotoImportCommand>(&canvas, source.get(), gated(gate));
        QVERIFY(source->importSelection()); QTRY_VERIFY(gate->started.load()); std::filesystem::path path;
        { std::lock_guard lock(gate->mutex); path = gate->source; } QVERIFY(std::filesystem::exists(path));
        source.reset(); QElapsedTimer elapsed; elapsed.start(); command.reset(); const auto microseconds = elapsed.nsecsElapsed() / 1000;
        QVERIFY(microseconds < 250000); QVERIFY(std::filesystem::exists(path)); gate->release.release(); QTRY_VERIFY(gate->finished.load());
        QVERIFY(gate->sourceAlive.load()); QTRY_VERIFY(!std::filesystem::exists(path)); QCOMPARE(canvas.document()->layers.size(), std::size_t(1));
        qInfo() << "photo_placement_close_us" << microseconds;
    }
    void aPhotoBatchCommitsOnceAndUndoRestoresTheWholeBatch() {
        const auto directory = folder(); QImage other(12, 20, QImage::Format_ARGB32); other.fill(Qt::green); QVERIFY(other.save(directory + "/Second.png"));
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Batch")); const auto path = storage.filePath("Files/batch.iisc");
        QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path))); auto *source = photos(canvas); QVERIFY(select(*source, directory)); source->selectAll(true);
        QSignalSpy revisions(&canvas, &EditorCanvas::revisionChanged), ready(source, &EditorCameraPhoto::importReady);
        QVERIFY(source->importSelection()); QTRY_COMPARE(ready.count(), 2); QCOMPARE(canvas.document()->layers.size(), std::size_t(3)); QCOMPARE(revisions.count(), 1);
        QVERIFY(canvas.undo()); QCOMPARE(canvas.document()->layers.size(), std::size_t(1)); QVERIFY(canvas.redo()); QCOMPARE(canvas.document()->layers.size(), std::size_t(3));
        EditorCanvas reopened; QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(path))); QCOMPARE(reopened.document()->layers.size(), std::size_t(3));
    }
    void aFailedPreparationPreservesTheDocumentAndAllowsRetry() {
        const auto directory = folder(); EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Failure"));
        const auto before = encodeIisc(*canvas.document()).bytes; const auto error = canvas.error();
        EditorCameraPhoto source; QVERIFY(select(source, directory)); int calls = 0;
        EditorPhotoImportCommand command(&canvas, &source, [&calls](const auto &input) {
            if (++calls == 1) throw std::runtime_error("Preparation fixture failure"); return dreamscapes::preparePhotoImport(input);
        });
        QSignalSpy ready(&source, &EditorCameraPhoto::importReady); QVERIFY(source.importSelection()); QTRY_VERIFY(!source.importing());
        QVERIFY(source.error().contains("fixture failure")); QCOMPARE(ready.count(), 0); QCOMPARE(canvas.error(), error); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        QVERIFY(source.importSelection()); QTRY_COMPARE(ready.count(), 1); QVERIFY(source.error().isEmpty()); QCOMPARE(canvas.document()->layers.size(), std::size_t(2));
    }
    void captureViewCancellationDoesNotEraseTheImportResult() {
        const auto directory = folder(); EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Error ownership"));
        EditorCameraPhoto source; QVERIFY(select(source, directory)); auto gate = std::make_shared<Gate>(); Release release{gate};
        EditorPhotoImportCommand command(&canvas, &source, [gate](const auto &) -> dreamscapes::PhotoImportOutput {
            gate->started = true; (void)gate->release.try_acquire_for(std::chrono::seconds(8)); throw std::runtime_error("Owned import failure");
        });
        QVERIFY(source.importSelection()); QTRY_VERIFY(gate->started.load()); source.cancel(); QVERIFY(source.importing());
        gate->release.release(); QTRY_VERIFY(!source.importing()); QVERIFY(source.importError().contains("Owned import failure"));
        source.clearError(); QVERIFY(source.importError().contains("Owned import failure")); QCOMPARE(canvas.document()->layers.size(), std::size_t(1));
    }
    void galleryFailuresDoNotPoisonTheSequentialQueue() {
        auto library = std::make_shared<BlockedLibrary>(std::make_shared<Gate>()); library->failScan = true;
        EditorCameraPhoto source(library, nullptr); source.loadGallery("iCloud Photo"); QTRY_VERIFY(!source.galleryBusy());
        QVERIFY(source.galleryStatus().contains("fixture failure")); QVERIFY(source.gallery().isEmpty());
        source.loadGallery("iCloud Photo"); QTRY_VERIFY(!source.galleryBusy()); QCOMPARE(source.gallery().size(), 1);
    }
    void supersededGalleryScansSkipNativeWork() {
        auto library = std::make_shared<PhotoQueueFixture>(); library->blockScan = true; Release release{library->gate};
        EditorCameraPhoto source(library, nullptr); source.loadGallery("iCloud Photo"); QTRY_VERIFY(library->gate->started.load());
        source.loadGallery("iCloud Photo"); source.loadGallery("iCloud Photo");
        library->gate->release.release(); QTRY_VERIFY(!source.galleryBusy()); QCOMPARE(source.gallery().size(), 3);
        QCOMPARE(library->order(), QStringList({"scan:1", "scan:2"}));
        QCOMPARE(library->gate->maximum.load(), 1); QVERIFY(library->allWorker.load()); QVERIFY(!library->timedOut.load());
    }
    void obsoletePreviewsCannotDelayAnAcceptedOriginal() {
        auto library = std::make_shared<PhotoQueueFixture>(); library->blockPreview = true; Release release{library->gate};
        EditorCameraPhoto source(library, nullptr); source.loadGallery("iCloud Photo"); QTRY_VERIFY(!source.galleryBusy());
        source.setSelected("0", false); source.requestThumbnail("0"); QTRY_VERIFY(library->gate->started.load());
        source.requestThumbnail("1"); source.requestThumbnail("2"); QSignalSpy imported(&source, &EditorCameraPhoto::importReady);
        QVERIFY(source.importSelection()); source.loadGallery("iCloud Photo");
        bool pulse = false; QTimer::singleShot(0, this, [&] { pulse = true; }); QTRY_VERIFY(pulse); QCOMPARE(imported.count(), 0);
        library->gate->release.release(); QTRY_COMPARE(imported.count(), 1); QTRY_VERIFY(!source.galleryBusy());
        QCOMPARE(library->order(), QStringList({"scan:1", "preview:0", "original:0", "scan:2"}));
        QCOMPARE(library->exports.load(), 1); QCOMPARE(library->gate->maximum.load(), 1); QVERIFY(library->allWorker.load()); QVERIFY(!library->timedOut.load());
        const QImage original(imported.first().first().toUrl().toLocalFile()); QVERIFY(!original.isNull()); QCOMPARE(original.pixelColor(0, 0), QColor(Qt::green));
    }
    void currentThumbnailRequestsKeepTheirOrder() {
        auto library = std::make_shared<PhotoQueueFixture>(); library->blockPreview = true; Release release{library->gate};
        EditorCameraPhoto source(library, nullptr); source.loadGallery("iCloud Photo"); QTRY_VERIFY(!source.galleryBusy());
        source.requestThumbnail("0"); QTRY_VERIFY(library->gate->started.load()); source.requestThumbnail("1"); source.requestThumbnail("2");
        library->gate->release.release(); QTRY_COMPARE(library->previews.load(), 3);
        QTRY_VERIFY([&] { for (const auto &item : source.gallery()) if (item.toMap()["thumbnail"].toUrl().isEmpty()) return false; return true; }());
        QCOMPARE(library->order(), QStringList({"scan:1", "preview:0", "preview:1", "preview:2"}));
        QCOMPARE(library->gate->maximum.load(), 1); QVERIFY(library->allWorker.load()); QVERIFY(!library->timedOut.load());
    }
    void galleryRefreshKeepsAnAlreadyRunningOriginal() {
        auto library = std::make_shared<PhotoQueueFixture>(); library->blockExport = true; Release release{library->gate};
        EditorCameraPhoto source(library, nullptr); source.loadGallery("iCloud Photo"); QTRY_VERIFY(!source.galleryBusy()); source.setSelected("0", false);
        QSignalSpy imported(&source, &EditorCameraPhoto::importReady); QVERIFY(source.importSelection()); QTRY_VERIFY(library->gate->started.load());
        source.loadGallery("iCloud Photo"); source.loadGallery("iCloud Photo");
        library->gate->release.release(); QTRY_COMPARE(imported.count(), 1); QTRY_VERIFY(!source.galleryBusy());
        QCOMPARE(library->order(), QStringList({"scan:1", "original:0", "scan:2"}));
        QCOMPARE(library->exports.load(), 1); QCOMPARE(library->gate->maximum.load(), 1); QVERIFY(library->allWorker.load()); QVERIFY(!library->timedOut.load());
    }
    void endingTheGalleryViewKeepsTheAcceptedOriginalAndTarget() {
        auto library = std::make_shared<PhotoQueueFixture>(); library->blockPreview = true; Release release{library->gate};
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Gallery view retirement"));
        EditorCameraPhoto source(library, nullptr); EditorPhotoImportCommand command(&canvas, &source); QObject presenter; source.attachGallery(&presenter);
        source.loadGallery("iCloud Photo"); QTRY_VERIFY(!source.galleryBusy()); source.setSelected("0", false);
        source.requestThumbnail("0"); QTRY_VERIFY(library->gate->started.load()); source.requestThumbnail("1"); source.requestThumbnail("2");
        const auto before = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision(); QSignalSpy imported(&source, &EditorCameraPhoto::importReady);
        QVERIFY(source.importSelection()); QVERIFY(source.detachGallery(&presenter)); QVERIFY(source.importing()); QCOMPARE(encodeIisc(*canvas.document()).bytes, before); QCOMPARE(canvas.revision(), revision);
        library->gate->release.release(); QTRY_COMPARE(imported.count(), 1); QTRY_COMPARE(canvas.document()->layers.size(), std::size_t(2));
        QCOMPARE(library->order(), QStringList({"scan:1", "preview:0", "original:0"})); QCOMPARE(library->exports.load(), 1); QCOMPARE(library->gate->maximum.load(), 1); QVERIFY(!source.importing());
    }
    void aReattachedGalleryRetriesOnlyItsCurrentCachedThumbnail() {
        auto library = std::make_shared<PhotoQueueFixture>(); library->blockPreview = true; Release release{library->gate};
        EditorCameraPhoto source(library, nullptr); QObject first, second; source.attachGallery(&first); source.loadGallery("iCloud Photo"); QTRY_VERIFY(!source.galleryBusy());
        source.requestThumbnail("0"); QTRY_VERIFY(library->gate->started.load()); source.requestThumbnail("1");
        QVERIFY(source.detachGallery(&first)); source.attachGallery(&second); source.requestThumbnail("1"); library->gate->release.release();
        const auto thumbnail = [&] { for (const auto &value : source.gallery()) { const auto item = value.toMap(); if (item["id"].toString() == "1") return item["thumbnail"].toUrl(); } return QUrl{}; };
        QTRY_VERIFY(!thumbnail().isEmpty()); QCOMPARE(library->order(), QStringList({"scan:1", "preview:0", "preview:1"})); QCOMPARE(library->previews.load(), 2);
        const QImage image(thumbnail().toLocalFile()); QVERIFY(!image.isNull()); QCOMPARE(image.size(), QSize(4, 4));
        const auto color = image.pixelColor(0, 0); QVERIFY(color.red() <= 2 && color.green() <= 2 && color.blue() >= 253);
    }
    void closingAcquisitionSkipsQueuedThumbnailSdkWork() {
        auto gate = std::make_shared<Gate>(); auto library = std::make_shared<BlockedLibrary>(gate);
        auto source = std::make_unique<EditorCameraPhoto>(library, nullptr);
        source->loadGallery("iCloud Photo"); QTRY_COMPARE(source->gallery().size(), 1); source->setSelected("one", false);
        QVERIFY(source->importSelection()); QTRY_VERIFY(gate->started.load()); source->requestThumbnail("one"); source.reset();
        gate->release.release(); QTRY_VERIFY(gate->finished.load()); QVERIFY(QThreadPool::globalInstance()->waitForDone(5000)); QCOMPARE(library->previews.load(), 0);
    }
    void previewDetachmentOnlyReleasesItsOwnOutput() {
        int monitors = 0;
        EditorCameraPhoto source(std::make_shared<BlockedLibrary>(std::make_shared<Gate>()), {}, [&] { ++monitors; return std::unique_ptr<QMediaDevices>{}; }, nullptr);
        auto previous = std::make_unique<QObject>(); QObject current;
        source.attachPreview(previous.get()); source.attachPreview(&current); QVERIFY(!source.detachPreview(previous.get()));
        previous.reset(); QVERIFY(source.detachPreview(&current)); QVERIFY(!source.detachPreview(&current));
        auto destroyed = std::make_unique<QObject>(); source.attachPreview(destroyed.get()); destroyed.reset();
        source.attachPreview(&current); QVERIFY(source.detachPreview(&current)); QCOMPARE(monitors, 0);
    }
    void importCompletionCanDeleteOrRestartThePlacementOwner_data() {
        QTest::addColumn<bool>("restart"); QTest::newRow("delete-owner") << false; QTest::newRow("next-request") << true;
    }
    void importCompletionCanDeleteOrRestartThePlacementOwner() {
        QFETCH(bool, restart); const auto directory = folder(); EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Completion observer"));
        EditorCameraPhoto source; QVERIFY(select(source, directory)); auto command = std::make_unique<EditorPhotoImportCommand>(&canvas, &source);
        int completed = 0; bool accepted = false;
        connect(&source, &EditorCameraPhoto::importReady, this, [&] {
            if (++completed != 1) return;
            if (restart) accepted = source.importSelection(); else command.reset();
        });
        QVERIFY(source.importSelection()); QTRY_COMPARE(completed, restart ? 2 : 1);
        if (restart) QVERIFY(accepted); else QVERIFY(!command);
        QCOMPARE(canvas.document()->layers.size(), std::size_t(restart ? 3 : 2)); QVERIFY(!source.importing());
    }
    void importObserversCanDeleteTheCanvasDuringAcceptance() {
        const auto directory = folder(); auto canvas = std::make_unique<EditorCanvas>(); QVERIFY(canvas->loadProjectPage(picture(), "Delete"));
        auto *source = photos(*canvas); QVERIFY(select(*source, directory)); const QPointer<EditorCameraPhoto> observed(source);
        connect(source, &EditorCameraPhoto::importStarted, this, [&] { canvas.reset(); });
        QVERIFY(!source->importSelection()); QVERIFY(!canvas); QVERIFY(observed.isNull());
    }
    void nativeRecordingPreparationKeepsAllFrames() {
        if (QString::fromUtf8(DREAMSCAPES_TEST_FFMPEG).isEmpty()) QSKIP("Native media helper is unavailable on this target");
        const auto path = storage.filePath("Recording.mp4"); QProcess process;
        int index = 0;
        for (const auto color : {Qt::red, Qt::green, Qt::blue}) {
            QImage frame(16, 12, QImage::Format_RGB32); frame.fill(color); QVERIFY(frame.save(storage.filePath(QString("recording-frame-%1.png").arg(index++))));
        }
        process.start(QString::fromUtf8(DREAMSCAPES_TEST_FFMPEG), {"-nostdin", "-y", "-framerate", "24", "-i", storage.filePath("recording-frame-%d.png"), "-frames:v", "3", "-c:v", "libx264", "-pix_fmt", "yuv420p", path});
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitCode(), 0);
        dreamscapes::PhotoImportInput input; input.originals.paths.emplace_back(path.toStdU16String()); input.frameRate = {24, 1};
        const auto prepared = dreamscapes::preparePhotoImport(input); QVERIFY2(prepared.ok(), prepared.error.c_str()); QCOMPARE(prepared.photos.size(), std::size_t(1));
        const auto *video = std::get_if<VideoAsset>(&prepared.photos.front().media); QVERIFY(video); QCOMPARE(video->frames.size(), std::size_t(3));
        QCOMPARE(video->frames.front().width, 16); QCOMPARE(video->frames.front().height, 12);
        QVERIFY(video->frames.front().pixels != video->frames.back().pixels);
    }
};
QTEST_MAIN(PhotoImportTests)
#include "tst_photoimport.moc"
