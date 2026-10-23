#include "App/Views/Editor/EditorProject.h"
#include <iiSharedCanvas/File/DocumentFile.h>
#include <iiSharedCanvas/Serialization/IiscCodec.h>
#include <iiFileProvider.h>
#include <SocietyDrive.h>
#include <SharedStorage.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

using namespace iiSharedCanvas;
class EditorFileInfoTests : public QObject {
    Q_OBJECT
    QTemporaryDir storage{DREAMSCAPES_TEST_DIRECTORY "/file-info-XXXXXX.societycontainer"};
    QString value(const QVariantMap &map, const char *key) { return map.value(QString::fromLatin1(key)).toString(); }
    static iiFileProvider::FileAuthor author(const QString &id) {
        QString error;
        auto result = iiFileProvider::FileAuthor::fromIisaccAccount({{"sub", id}, {"email", id + "@example.com"},
            {"displayName", id}, {"userId", "@file_" + id}, {"societyCloudMembership", "Free"}},
            QUrl("https://iisacc.com"), QDateTime::fromString("2026-10-14T01:00:00Z", Qt::ISODate), &error);
        if (!result) qFatal("Invalid author fixture: %s", qPrintable(error));
        auto metadata = result->metadata();
        metadata.details.organization = "Organization " + id;
        metadata.details.links = {{"homepage", "Profile " + id, QUrl("https://example.com/" + id)}};
        metadata.details.identifiers = {{"test", id}};
        metadata.attribution.roles = {"artist", "editor"};
        metadata.attribution.credit = "Credit " + id;
        metadata.device = iiFileProvider::AuthorDevice{QString(64, id.front()), "pc", "Device " + id, "macOS", "26", "com.iisacc.dreamscapes", "1"};
        if (!result->setMetadata(metadata, &error)) qFatal("Invalid author metadata fixture: %s", qPrintable(error));
        return *result;
    }
private slots:
    void initTestCase() {
        QVERIFY(storage.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        qputenv("SOCIETY_CONTAINER_PATH", storage.path().toUtf8());
    }
    void allFigmaBindingsExistWithEmptyAndReadyDocuments() {
        QFile fixture(DREAMSCAPES_SOURCE_DIRECTORY "/tests/fixtures/EditorFileBindings.json");
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto views = QJsonDocument::fromJson(fixture.readAll()).object()["views"].toArray();
        QSet<QString> expected;
        const QList<int> counts{108, 94, 26};
        QCOMPARE(views.size(), 3);
        for (int i = 0; i < views.size(); ++i) {
            const auto keys = views[i].toObject()["bindings"].toArray();
            QCOMPARE(keys.size(), counts[i]);
            for (const auto &key : keys) { QVERIFY(!expected.contains(key.toString())); expected.insert(key.toString()); }
        }
        QCOMPARE(expected.size(), 228);
        EditorProject project;
        for (int ready = 0; ready < 2; ++ready) {
            const auto info = project.fileInformation();
            QCOMPARE(QSet<QString>(info.keyBegin(), info.keyEnd()), expected);
            for (const auto &entry : info) { QCOMPARE(entry.metaType(), QMetaType::fromType<QString>()); QVERIFY(!entry.toString().isEmpty()); }
            QCOMPARE(value(info, "EditorCanvas.documentReady"), ready ? "Yes" : "No");
            if (!ready) {
                QCOMPARE(value(info, "Document.extent.width/height"), "Not available");
                QVERIFY(project.createCanvas({{"name", "Real canvas"}, {"width", 16}, {"height", 8}, {"unit", "px"}, {"ppi", 300}, {"background", "Transparent"}}));
            } else {
                QCOMPARE(value(info, "Document.extent.width/height"), "16 × 8 px");
                QCOMPARE(value(info, "EditorCanvas.documentName"), "Real canvas");
                QCOMPARE(value(info, "Document.assets.count"), "0");
                QCOMPARE(value(info, "Document.audioAssets.count"), "0");
                QCOMPARE(value(info, "Document.stableDiffusionMetadata"), "Not recorded");
                QCOMPARE(value(info, "EditorProject.modified"), "Yes");
            }
        }
    }
    void multipleRecordsPrecisionAndReadOnlyPresentation() {
        EditorCanvas canvas;
        QVERIFY(canvas.createCanvas({{"width", 16}, {"height", 8}, {"unit", "px"}, {"background", "Transparent"}}));
        auto d = *canvas.document();
        d.stableDiffusionMetadata = StableDiffusionMetadata{};
        auto &g = *d.stableDiffusionMetadata;
        g.positivePrompt = "<b>literal prompt</b>\nwith a long second line";
        g.negativePrompt = "recorded negative";
        g.batchSize = 1; g.clipSkip = 1;
        g.samplingPasses = {{"first", std::numeric_limits<std::uint64_t>::max(), 20, 0.0, "euler", "normal", 0.0, 0, 20},
                            {"second", 0, 30, 4.5, "heun", "karras", 1.0, 0, 30}};
        g.models = {{"model", "one", "aaa", "sha256", "file:///one"}, {"vae", "two", "bbb", "sha256", "file:///two"}};
        g.loras = {{"first", "hash-1", 0, 1}, {"second", "hash-2", 0.5, 0}};
        g.comfyUi.promptJson = "{\"first\":1}"; g.comfyUi.workflowJson = "{\"second\":2}";
        g.comfyUi.extraPngInfo = {{"png-key", "\"png-value\""}}; g.extraParameters = {{"custom", "value"}};
        d.timeline.frameCount = 48;
        d.artboards = {{"first", "First", {{0, 0}, {16, 8}}, 0, true}, {"second", "Second", {{64, 0}, {8, 8}}, 0xff123456U, false}};
        d.audioAssets = {{"audio", 48000, 2, std::vector<std::int16_t>(96000)}};
        d.audioTracks = {{"track", "Track", false, 0, {{"clip", "Clip", "audio", 0, 24, 0, 0, true}}}};
        d.authorship.setAuthor(author("a")); d.authorship.recordChange();
        d.authorship.setAuthor(author("b")); d.authorship.recordChange();
        QVERIFY(d.authorship.setLinksFromStrings({"[Documentation|https://example.com/doc?x=1]", "[Reference|https://example.com/ref]"}));
        QVERIFY2(canvas.loadProjectPage(std::move(d), "Metadata fixture"), qPrintable(canvas.error()));
        const auto bytes = encodeIisc(*canvas.document()).bytes;
        const auto revision = canvas.revision();
        const auto info = canvas.fileInformation();
        QCOMPARE(value(info, "StableDiffusionSamplingPass.seed"), "[01] 18446744073709551615\n[02] 0");
        QCOMPARE(value(info, "StableDiffusionSamplingPass.cfgScale"), "[01] 0\n[02] 4.5");
        QCOMPARE(value(info, "StableDiffusionMetadata.positivePrompt"), QStringLiteral("<b>literal prompt</b>\nwith a long second line"));
        QVERIFY(value(info, "StableDiffusionModelResource.name").contains("two"));
        QVERIFY(value(info, "StableDiffusionLora.clipStrength").endsWith("[02] 0"));
        QVERIFY(value(info, "AuthorDetails.organization").contains("Organization b"));
        QVERIFY(value(info, "AuthorLink.url").contains("https://example.com/b"));
        QVERIFY(value(info, "AuthorDevice.name").contains("Device b"));
        QVERIFY(value(info, "FileLink.urlText").contains("https://example.com/ref"));
        QCOMPARE(value(info, "AudioAsset.derivedDuration"), "audio · 1 s");
        QCOMPARE(value(info, "AudioTrackLayer.muted"), "No");
        QCOMPARE(value(info, "Document.artboards.count"), "2");
        QCOMPARE(value(info, "Artboard.visible"), "[01] Yes\n[02] No");
        QCOMPARE(value(info, "Document.timeline.derivedDuration"), "2 s");
        QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes);
        QCOMPARE(canvas.revision(), revision);
        const auto revisionInfo = value(info, "Authorship.revision");
        QSignalSpy updates(&canvas, &EditorCanvas::fileInformationChanged);
        QSignalSpy viewUpdates(&canvas, &EditorCanvas::fileViewInformationChanged);
        canvas.setFrame(1); canvas.setPagePresentation(true);
        QCOMPARE(updates.size(), 0);
        QVERIFY(viewUpdates.size() >= 2);
        QCOMPARE(value(canvas.fileInformation(), "EditorCanvas.frame"), "1");
        QCOMPARE(value(canvas.fileViewInformation(), "EditorCanvas.frame"), "1");
        QCOMPARE(value(canvas.fileInformation(), "Authorship.revision"), revisionInfo);
        QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes);
        QCOMPARE(canvas.revision(), revision);
    }
    void sparseAndVideoContentUsesDocumentValues() {
        EditorCanvas canvas;
        Document sparse;
        sparse.extent = {64, 32}; sparse.canvasMode = CanvasMode::Infinite;
        sparse.infiniteCanvas = {{-100, 80}, 256};
        sparse.assets.emplace_back(ChunkedRasterAsset{"chunks", {{-2, 3, makeRasterLayer(256, 256)}}});
        sparse.layers.emplace_back(StaticBitmapLayer{{"layer", "Sparse"}, StaticSource{"chunks"}});
        QVERIFY(canvas.loadProjectPage(std::move(sparse), "Sparse"));
        const auto info = canvas.fileInformation();
        QCOMPARE(value(info, "Document.canvasMode"), "Infinite");
        QCOMPARE(value(info, "Document.infiniteCanvas.origin.x/y"), "-100, 80");
        QCOMPARE(value(info, "Document.infiniteCanvas.chunkSize"), "256 px");
        QCOMPARE(value(info, "ChunkedRasterAsset.chunks.count"), "1");
        QVERIFY(value(info, "RasterChunk.column/row").contains("-2, 3"));
        QVERIFY(value(info, "RasterChunk.pixels.width/height").contains("256 × 256 px"));
        QCOMPARE(value(info, "EditorCanvas.specification.ppi"), "Not recorded");
        Document video; video.extent = {4, 4}; video.timeline = {{30000, 1001}, 60};
        video.assets.emplace_back(VideoAsset{"video", {24, 1}, {makeRasterLayer(4, 4), makeRasterLayer(4, 4)}});
        LayerProperties properties; properties.id = "video-layer"; properties.frameRange = LayerFrameRange{2, 59};
        video.layers.emplace_back(VideoLayer{properties, StaticSource{"video"}, {0, 2, VideoEndBehavior::Hold}});
        QVERIFY(canvas.loadProjectPage(std::move(video), "Video"));
        const auto media = canvas.fileInformation();
        QCOMPARE(value(media, "VideoAsset.count"), "1");
        QCOMPARE(value(media, "VideoLayer.count"), "1");
        QCOMPARE(value(media, "VideoAsset.frames.count"), "video · 2");
        QCOMPARE(value(media, "VideoPlayback.sourceOutFrame"), "video-layer · 2 (exclusive)");
        QCOMPARE(value(media, "VideoPlayback.endBehavior"), "video-layer · Hold");
        QCOMPARE(value(media, "LayerProperties.frameRange.firstFrame/lastFrame"), "video-layer · 2 — 59 (inclusive)");
        QCOMPARE(value(media, "Document.timeline.frameRate.numerator/denominator"), "30000/1001 fps");
        QCOMPARE(value(media, "Document.timeline.derivedDuration"), "2.002 s");
    }
    void savedStorageRevisionAndMissingFileNeverWrite() {
        EditorProject project;
        QVERIFY(project.createCanvas({{"width", 16}, {"height", 8}, {"unit", "px"}, {"background", "Transparent"}}));
        QVERIFY(project.currentCanvas()->addPaintLayer());
        const auto volume = iiSocietyContainer::SharedStorage::open(storage.path());
        QVERIFY(volume);
        const auto path = volume->filePath(iiSocietyContainer::StoreSection::Files, "real.iisc");
        QVERIFY2(project.saveDocumentAs(QUrl::fromLocalFile(path)), qPrintable(project.error()));
        const auto before = iiFileProvider::File::read(path);
        const auto revision = project.currentCanvas()->revision();
        std::uint64_t storedRevision = 999;
        QVERIFY(DocumentFile::readRevision(path.toStdString(), storedRevision).ok());
        for (int i = 0; i < 3; ++i) {
            const auto info = project.fileInformation();
            QCOMPARE(value(info, "Derived.storageFormat"), "SQLite working file");
            QCOMPARE(value(info, "DocumentFile.revision"), QString::number(storedRevision));
            QCOMPARE(value(info, "EditorProject.modified"), "No");
            QCOMPARE(value(info, "Alias.path"), path);
            QCOMPARE(value(info, "Alias.exists"), "Yes");
        }
        QCOMPARE(iiFileProvider::File::read(path), before);
        QCOMPARE(project.currentCanvas()->revision(), revision);
        QSignalSpy updates(&project, &EditorProject::fileInformationChanged);
        QVERIFY(project.currentCanvas()->setLayerName(project.currentCanvas()->selectedLayerId(), "Changed"));
        QVERIFY(updates.size() > 0);
        QVERIFY(value(project.fileInformation(), "DocumentFile.revision") != QString::number(storedRevision));
        EditorCanvas missing;
        QVERIFY(!missing.openDocumentSource(QUrl::fromLocalFile(storage.filePath("missing.iisc"))));
        QVERIFY(value(missing.fileInformation(), "EditorCanvas.error") != "No errors");
        QVERIFY(!QFile::exists(storage.filePath("missing.iisc")));
    }
};
QTEST_MAIN(EditorFileInfoTests)
#include "tst_editorfileinfo.moc"
