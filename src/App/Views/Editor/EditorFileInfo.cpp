#include "EditorCanvas.h"
#include "EditorProject.h"
#include <iiSharedCanvas/File/DocumentFile.h>
#include <iiSharedCanvas/Document/Document.h>
#include <iiFileProvider.h>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <type_traits>
#include <tuple>

namespace {
using namespace iiSharedCanvas;
const QString missing = QStringLiteral("Not recorded");
const QString unavailable = QStringLiteral("Not available");
const QString none = QStringLiteral("None recorded");
QString text(const std::string &value) { return value.empty() ? missing : QString::fromStdString(value); }
QString text(const QString &value) { return value.isEmpty() ? missing : value; }
QString boolean(bool value) { return value ? QStringLiteral("Yes") : QStringLiteral("No"); }
template<class T> QString number(T value) {
    if constexpr (std::is_floating_point_v<T>) return QString::number(value, 'g', 15);
    else if constexpr (std::is_signed_v<T>) return QString::number(static_cast<qlonglong>(value));
    else return QString::number(static_cast<qulonglong>(value));
}
template<class T> QString optional(const std::optional<T> &value) { return value ? number(*value) : missing; }
QString dimensions(std::int64_t width, std::int64_t height) { return number(width) + " × " + number(height) + " px"; }
QString coordinates(std::int64_t x, std::int64_t y) { return number(x) + ", " + number(y); }
QString rate(FrameRate value) { return number(value.numerator) + "/" + number(value.denominator) + " fps"; }
QString seconds(double value) { return number(value) + " s"; }
QString region(CanvasRegion value) { return coordinates(value.origin.x, value.origin.y) + " · " + dimensions(value.extent.width, value.extent.height); }
QString jsonText(const QJsonValue &value) {
    if (value.isNull() || value.isUndefined()) return missing;
    if (value.isBool()) return boolean(value.toBool());
    if (value.isString()) return text(value.toString());
    if (value.isDouble()) return number(value.toDouble());
    if (value.isArray()) {
        QStringList entries;
        for (const auto &entry : value.toArray()) entries << jsonText(entry);
        return entries.isEmpty() ? none : entries.join(", ");
    }
    return missing;
}
QJsonValue at(QJsonValue value, const QString &path) {
    for (const auto &part : path.split('.')) value = value.toObject().value(part);
    return value;
}
template<class C, class F> QString records(const C &collection, F value) {
    if (collection.empty()) return none;
    QStringList lines;
    std::size_t index = 0;
    for (const auto &entry : collection) {
        const auto content = value(entry);
        lines << (collection.size() > 1 ? "[" + number(++index).rightJustified(2, '0') + "] " + content : content);
    }
    return lines.join('\n');
}
template<class T> std::size_t assetCount(const Document &document) {
    return std::ranges::count_if(document.assets, [](const Asset &asset) { return std::holds_alternative<T>(asset); });
}
QString metadataEntries(const std::vector<StableDiffusionMetadataEntry> &entries) {
    return records(entries, [](const auto &entry) { return QString::fromStdString(entry.key) + " = " + QString::fromStdString(entry.value); });
}
}

QVariantMap EditorCanvas::fileInformation() const
{
    using namespace iiSharedCanvas;
    QVariantMap values;
    const Document empty;
    const Document &d = documentReady() && document() ? *document() : empty;
    const auto put = [&values](const char *key, const QString &value) { values.insert(QString::fromLatin1(key), value); };
    const auto alias = [this](const char *key) {
        const auto value = m_documentAlias.value(QString::fromLatin1(key));
        if (!value.isValid() || value.isNull()) return missing;
        if (value.metaType().id() == QMetaType::Bool) return boolean(value.toBool());
        return text(value.toString());
    };

    // The storage owner supplies Alias snapshots. Never rescan/hash Society files
    // or open an authoring DocumentFile while presenting their information.
    const auto inspectionKey = filePath() + '\n' + alias("version") + '\n' + alias("exists") + '\n' + alias("available");
    if (m_fileInspectionKey != inspectionKey) {
        m_fileInspectionKey = inspectionKey;
        m_fileStorageFormat = unavailable;
        m_fileStoredRevision = unavailable;
        if (!filePath().isEmpty() && m_documentAlias.value("exists").toBool()
            && m_documentAlias.value("available").toBool()) {
            try {
                const auto header = iiFileProvider::File::readPrefix(filePath(), 16);
                if (header == QByteArray("SQLite format 3\0", 16)) {
                    m_fileStorageFormat = QStringLiteral("SQLite working file");
                    std::uint64_t revision = 0;
                    const auto result = DocumentFile::readRevision(filePath().toStdString(), revision);
                    if (result.ok()) m_fileStoredRevision = number(revision);
                } else if (header.startsWith("IISC")) {
                    m_fileStorageFormat = QStringLiteral("Serialized IISC snapshot");
                    m_fileStoredRevision = QStringLiteral("Not applicable");
                } else {
                    m_fileStorageFormat = QStringLiteral("Unrecognized storage format");
                }
            } catch (const std::exception &) { /* Availability/error remain owned by FileSystem. */ }
        }
    }
    put("EditorCanvas.documentName", documentName());
    put("Alias.name", alias("name"));
    put("Document.formatVersion.major/minor", "IISC " + number(d.formatVersion.major) + "." + number(d.formatVersion.minor));
    put("Derived.storageFormat", filePath().isEmpty() ? (documentReady() ? QStringLiteral("In-memory document") : unavailable) : m_fileStorageFormat);
    put("Alias.size", m_documentAlias.contains("size") ? number(m_documentAlias.value("size").toULongLong()) + " bytes" : missing);
    put("Alias.modified", alias("modified"));
    put("Alias.isImage", alias("isImage"));

    const auto &g = d.stableDiffusionMetadata;
    const StableDiffusionMetadata blankGeneration;
    const auto &metadata = g ? *g : blankGeneration;
    put("StableDiffusionMetadata.positivePrompt", text(metadata.positivePrompt));
    put("StableDiffusionMetadata.negativePrompt", text(metadata.negativePrompt));
    put("StableDiffusionMetadata.outputExtent.width/height", metadata.outputExtent ? dimensions(metadata.outputExtent->width, metadata.outputExtent->height) : missing);
    put("StableDiffusionMetadata.batchSize", optional(metadata.batchSize));
    put("StableDiffusionMetadata.clipSkip", optional(metadata.clipSkip));
    put("StableDiffusionMetadata.software", text(metadata.software));
    put("StableDiffusionMetadata.softwareVersion", text(metadata.softwareVersion));
    put("StableDiffusionMetadata.createdAt", text(metadata.createdAt));
    put("Document.stableDiffusionMetadata", g ? "Recorded" : missing);
    put("StableDiffusionMetadata.positivePrompt/negativePrompt", "Positive: " + text(metadata.positivePrompt) + "\nNegative: " + text(metadata.negativePrompt));
    put("StableDiffusionMetadata.samplingPasses", number(metadata.samplingPasses.size()));
    put("StableDiffusionMetadata.models", number(metadata.models.size()));
    put("StableDiffusionMetadata.loras", number(metadata.loras.size()));
#define PASS(member, format) put("StableDiffusionSamplingPass." #member, records(metadata.samplingPasses, [](const auto &p) { return format(p.member); }))
    PASS(nodeId, text); PASS(seed, optional); PASS(steps, optional); PASS(cfgScale, optional);
    PASS(samplerName, text); PASS(scheduler, text); PASS(denoiseStrength, optional);
    PASS(startStep, optional); PASS(endStep, optional);
#undef PASS
#define MODEL(member) put("StableDiffusionModelResource." #member, records(metadata.models, [](const auto &m) { return text(m.member); }))
    MODEL(role); MODEL(name); MODEL(hash); MODEL(hashType); MODEL(uri);
#undef MODEL
#define LORA(member, format) put("StableDiffusionLora." #member, records(metadata.loras, [](const auto &l) { return format(l.member); }))
    LORA(name, text); LORA(hash, text); LORA(modelStrength, number); LORA(clipStrength, number);
#undef LORA
    put("StableDiffusionMetadata.generationParametersText", text(metadata.generationParametersText));
    put("ComfyUiMetadata.promptJson", text(metadata.comfyUi.promptJson));
    put("ComfyUiMetadata.workflowJson", text(metadata.comfyUi.workflowJson));
    put("ComfyUiMetadata.extraPngInfo[].key/value", metadataEntries(metadata.comfyUi.extraPngInfo));
    put("StableDiffusionMetadata.extraParameters[].key/value", metadataEntries(metadata.extraParameters));
    put("ComfyUiMetadata.promptJson/workflowJson", "Prompt: " + text(metadata.comfyUi.promptJson) + "\nWorkflow: " + text(metadata.comfyUi.workflowJson));
    put("StableDiffusionMetadata.extraParameters", number(metadata.extraParameters.size()));

    const auto authorship = d.authorship.toJson();
    const auto authors = authorship.value("authors").toArray();
    const auto authorName = [](const QJsonObject &record) {
        const auto author = iiFileProvider::FileAuthor::fromJson(record.value("author").toObject());
        return author ? text(author->displayLabel()) : missing;
    };
    put("Authorship.schemaVersion", number(iiFileProvider::Authorship::SchemaVersion));
    put("Authorship.revision", number(d.authorship.revision()));
    put("Authorship.modifiedAt", jsonText(authorship.value("modifiedAt")));
    put("Authorship.firstEditor", authors.empty() ? none : authorName(authors.first().toObject()));
    QStringList participants;
    QString lastAuthor = missing;
    for (qsizetype i = 0; i < authors.size(); ++i) {
        const auto record = authors[i].toObject();
        if (i) participants << authorName(record);
        if (record.value("key") == authorship.value("lastAuthor")) lastAuthor = authorName(record);
    }
    put("Authorship.participants", participants.empty() ? none : participants.join('\n'));
    put("Authorship.lastAuthor", lastAuthor);
    for (const auto &field : {"key", "firstChangedAt", "lastChangedAt"})
        put(qPrintable("Authorship.authors[]." + QString::fromLatin1(field)), records(authors, [field](const auto &a) { return jsonText(a.toObject().value(field)); }));
    put("Authorship.isEmpty", boolean(d.authorship.isEmpty()));
    put("Authorship.links", number(d.authorship.links().size()));

    // toJson exports only persisted author snapshots; runtime tokens/sessions are
    // never exposed by this bridge. Index prefixes keep every recorded author.
    const std::pair<const char *, const char *> authorFields[] = {
        {"FileAuthor.SchemaVersion", "schemaVersion"},
        {"IisaccAccount.sub", "account.sub"},
        {"IisaccAccount.userId", "account.userId"},
        {"IisaccAccount.displayName", "account.displayName"},
        {"IisaccAccount.email", "account.email"},
        {"IisaccAccount.societyCloudMembership", "account.societyCloudMembership"},
        {"IisaccAccount.avatarUrl", "account.avatarUrl"},
        {"AuthorMetadata.serviceOrigin", "serviceOrigin"},
        {"AuthorMetadata.capturedAt", "capturedAt"},
        {"AuthorDetails.fullName", "details.fullName"},
        {"AuthorDetails.givenName", "details.givenName"},
        {"AuthorDetails.additionalName", "details.additionalName"},
        {"AuthorDetails.familyName", "details.familyName"},
        {"AuthorDetails.pseudonym", "details.pseudonym"},
        {"AuthorDetails.biography", "details.biography"},
        {"AuthorDetails.organization", "details.organization"},
        {"AuthorDetails.organizationId", "details.organizationId"},
        {"AuthorDetails.department", "details.department"},
        {"AuthorDetails.team", "details.team"},
        {"AuthorDetails.jobTitle", "details.jobTitle"},
        {"AuthorDetails.contactEmail", "details.contactEmail"},
        {"AuthorDetails.phoneNumber", "details.phoneNumber"},
        {"AuthorDetails.locale", "details.locale"},
        {"AuthorDetails.timeZone", "details.timeZone"},
        {"AuthorDetails.countryCode", "details.countryCode"},
        {"AuthorDetails.region", "details.region"},
        {"AuthorDetails.city", "details.city"},
        {"FileAttribution.roles", "attribution.roles"},
        {"FileAttribution.credit", "attribution.credit"},
        {"FileAttribution.copyrightNotice", "attribution.copyrightNotice"},
        {"FileAttribution.licenseIdentifier", "attribution.licenseIdentifier"},
        {"FileAttribution.licenseUrl", "attribution.licenseUrl"},
        {"FileAttribution.documentId", "attribution.documentId"},
        {"FileAttribution.projectId", "attribution.projectId"},
        {"FileAttribution.workspaceId", "attribution.workspaceId"},
        {"FileAttribution.createdAt", "attribution.createdAt"},
        {"FileAttribution.modifiedAt", "attribution.modifiedAt"},
        {"AuthorDevice.id", "device.id"},
        {"AuthorDevice.type", "device.type"},
        {"AuthorDevice.name", "device.name"},
        {"AuthorDevice.platform", "device.platform"},
        {"AuthorDevice.osVersion", "device.osVersion"},
        {"AuthorDevice.appId", "device.appId"},
        {"AuthorDevice.appVersion", "device.appVersion"},
    };
    for (const auto &[key, path] : authorFields)
        put(key, records(authors, [path](const auto &a) { return jsonText(at(a.toObject().value("author"), QString::fromLatin1(path))); }));
    for (const auto &[prefix, collection, members] : {
        std::tuple{"AuthorLink.", "links", QStringList{"relation", "label", "url"}},
        std::tuple{"AuthorIdentifier.", "identifiers", QStringList{"scheme", "value"}}}) {
        for (const auto &member : members)
            put(qPrintable(QString::fromLatin1(prefix) + member), records(authors, [&](const auto &a) {
                const auto entries = at(a.toObject().value("author"), "details." + QString::fromLatin1(collection)).toArray();
                return records(entries, [&](const auto &entry) { return jsonText(entry.toObject().value(member)); });
            }));
    }
    put("AuthorMetadata.device", records(authors, [](const auto &a) { return a.toObject().value("author").toObject().value("device").isObject() ? QStringLiteral("Recorded") : missing; }));
    const auto links = d.authorship.links();
    put("FileLink.name", records(links, [](const auto &link) { return text(link.name()); }));
    put("FileLink.urlText", records(links, [](const auto &link) { return text(link.urlText()); }));

    const auto origin = canvasOrigin(d);
    const auto bounds = documentViewRegion(d);
    put("Document.canvasMode", d.canvasMode == CanvasMode::Finite ? "Finite" : "Infinite");
    put("Document.extent.width/height", dimensions(d.extent.width, d.extent.height));
    put("canvasOrigin(Document)", coordinates(origin.x, origin.y));
    put("documentViewRegion(Document)", region(bounds));
    put("Document.extent/Document.layers/Document.timeline", dimensions(d.extent.width, d.extent.height) + " · " + number(d.layers.size()) + " layers · " + number(d.timeline.frameCount) + " frames");
    const auto spec = [this](const char *key) { return text(m_specification.value(QString::fromLatin1(key)).toString()); };
    put("EditorCanvas.specification.width/height", m_specification.contains("width") && m_specification.contains("height") ? spec("width") + " × " + spec("height") : missing);
    put("EditorCanvas.specification.unit", spec("unit"));
    put("EditorCanvas.specification.ppi", m_specification.contains("ppi") ? spec("ppi") + " PPI" : missing);
    put("EditorCanvas.specification.background", spec("background"));
    put("EditorCanvas.specification.name", spec("name"));
    put("EditorCanvas.specification.sourceCount", spec("sourceCount"));
    put("EditorCanvas.specification.unit/ppi", spec("unit") + " · " + spec("ppi") + " PPI");
#define BOARD(member, expression) put("Artboard." member, records(d.artboards, [](const auto &b) { return expression; }))
    BOARD("id", text(b.id)); BOARD("name", text(b.name));
    BOARD("region.origin.x/y", coordinates(b.region.origin.x, b.region.origin.y));
    BOARD("region.extent.width/height", dimensions(b.region.extent.width, b.region.extent.height));
    BOARD("backgroundArgb", "#" + QString::number(b.backgroundArgb, 16).rightJustified(8, '0').toUpper() + " ARGB");
    BOARD("visible", boolean(b.visible));
#undef BOARD
    put("Document.artboards", records(d.artboards, [](const auto &b) { return text(b.id) + " · " + text(b.name); }));
    put("EditorProject.canvasCount", number(documentReady() ? std::max<std::size_t>(1, d.artboards.size()) : 1));
    put("EditorProject.currentIndex", number(activeArtboardIndex()));
    put("Document.artboards.count", number(d.artboards.size()));
    put("EditorCanvas.activeArtboardIndex", number(activeArtboardIndex()));
    put("EditorCanvas.editingWidth/Height", documentReady() ? dimensions(editingWidth(), editingHeight()) : unavailable);
    put("EditorCanvas.editingOrigin", documentReady() ? coordinates(qRound64(editingOrigin().x()), qRound64(editingOrigin().y())) : unavailable);
    put("EditorCanvas.pagePresentation", boolean(pagePresentation()));
    put("Document.layers/Document.assets", number(d.layers.size()) + " layers / " + number(d.assets.size()) + " assets");
    put("Document.layers.count", number(d.layers.size()));
#define KIND(name) put("LayerKind." #name, number(std::ranges::count_if(d.layers, [](const Layer &l) { return layerKind(l) == LayerKind::name; })))
    KIND(StaticBitmap); KIND(StaticVector); KIND(DynamicBitmap); KIND(DynamicVector);
#undef KIND
    put("VideoLayer.count", number(std::ranges::count_if(d.layers, [](const Layer &l) { return std::holds_alternative<VideoLayer>(l); })));
    put("LayerRole.ControlNet.count", number(std::ranges::count_if(d.layers, [](const Layer &l) { return layerRole(l) == LayerRole::ControlNet; })));
    put("SemanticSegmentLayer.count", number(std::ranges::count_if(d.layers, [](const Layer &l) { return std::holds_alternative<SemanticSegmentLayer>(l); })));
    put("Document.assets.count", number(d.assets.size()));
#define ASSET(name) put(#name ".count", number(assetCount<name>(d)))
    ASSET(RasterAsset); ASSET(VectorAsset); ASSET(ChunkedRasterAsset); ASSET(PoseAsset);
    ASSET(DepthAsset); ASSET(LineArtAsset); ASSET(CannyAsset); ASSET(ScribbleAsset);
    ASSET(MlsdAsset); ASSET(NormalMapAsset); ASSET(ShuffleAsset); ASSET(TileAsset);
    ASSET(ReferenceAsset); ASSET(IpAdapterAsset); ASSET(VideoAsset);
#undef ASSET
    std::size_t keyframes = 0, motions = 0, clipCount = 0, chunkCount = 0;
    for (const auto &f : d.frames) keyframes += f.keyframes.size();
    for (const auto &l : d.layers) motions += layerProperties(l).motion.size();
    for (const auto &t : d.audioTracks) clipCount += t.clips.size();
    std::vector<const ChunkedRasterAsset *> chunked;
    std::vector<const VideoAsset *> videos;
    std::vector<const VideoLayer *> videoLayers;
    for (const auto &a : d.assets) {
        if (const auto *chunks = std::get_if<ChunkedRasterAsset>(&a)) { chunked.push_back(chunks); chunkCount += chunks->chunks.size(); }
        if (const auto *video = std::get_if<VideoAsset>(&a)) videos.push_back(video);
    }
    for (const auto &l : d.layers) if (const auto *video = std::get_if<VideoLayer>(&l)) videoLayers.push_back(video);
    put("Frame.keyframes.count", number(keyframes));
    put("Document.frames.count", number(d.frames.size()));
    put("LayerProperties.motion.count", number(motions));
    put("Document.audioAssets.count", number(d.audioAssets.size()));
    put("Document.audioTracks.count", number(d.audioTracks.size()));
    put("AudioTrackLayer.clips.count", number(clipCount));
    put("Document.timeline", rate(d.timeline.frameRate) + " · " + number(d.timeline.frameCount) + " frames");
    put("Document.timeline.frameRate.numerator/denominator", rate(d.timeline.frameRate));
    put("Document.timeline.frameCount", number(d.timeline.frameCount));
    put("Document.timeline.derivedDuration", d.timeline.frameRate.numerator ? seconds(double(d.timeline.frameCount) * d.timeline.frameRate.denominator / d.timeline.frameRate.numerator) : unavailable);
    put("EditorCanvas.frame", number(frame()));
    put("ChunkedRasterAsset.chunks.count", number(chunkCount));
    put("ChunkedRasterAsset.id", records(chunked, [](const auto *a) { return text(a->id); }));
    put("Document.infiniteCanvas.origin.x/y", d.canvasMode == CanvasMode::Infinite ? coordinates(d.infiniteCanvas.origin.x, d.infiniteCanvas.origin.y) : "Not applicable");
    put("Document.infiniteCanvas.chunkSize", d.canvasMode == CanvasMode::Infinite ? number(d.infiniteCanvas.chunkSize) + " px" : "Not applicable");
    put("RasterChunk.column/row", records(chunked, [](const auto *a) { return text(a->id) + "\n" + records(a->chunks, [](const auto &c) { return coordinates(c.column, c.row); }); }));
    put("RasterChunk.pixels.width/height", records(chunked, [](const auto *a) { return text(a->id) + "\n" + records(a->chunks, [](const auto &c) { return coordinates(c.column, c.row) + " · " + dimensions(c.pixels.width, c.pixels.height); }); }));
    put("Derived.bitmapPixelModel", "8-bit channels · sRGB · ARGB32");
    put("VideoAsset.id", records(videos, [](const auto *v) { return text(v->id); }));
    put("VideoAsset.frameRate.numerator/denominator", records(videos, [](const auto *v) { return text(v->id) + " · " + rate(v->frameRate); }));
    put("VideoAsset.frames.count", records(videos, [](const auto *v) { return text(v->id) + " · " + number(v->frames.size()); }));
    put("VideoAsset.frames[].width/height", records(videos, [](const auto *v) {
        return text(v->id) + "\n" + records(v->frames, [](const auto &f) { return dimensions(f.width, f.height); });
    }));
    put("VideoPlayback.sourceInFrame", records(videoLayers, [](const auto *v) { return text(v->properties.id) + " · " + number(v->playback.sourceInFrame); }));
    put("VideoPlayback.sourceOutFrame", records(videoLayers, [](const auto *v) { return text(v->properties.id) + " · " + (v->playback.sourceOutFrame ? number(*v->playback.sourceOutFrame) + " (exclusive)" : "Asset end"); }));
    put("VideoPlayback.endBehavior", records(videoLayers, [](const auto *v) { return text(v->properties.id) + " · " + (v->playback.endBehavior == VideoEndBehavior::Hold ? "Hold" : "Transparent"); }));
    put("LayerProperties.frameRange.firstFrame/lastFrame", records(d.layers, [](const auto &l) {
        const auto &p = layerProperties(l);
        return text(p.id) + " · " + (p.frameRange ? number(p.frameRange->firstFrame) + " — " + number(p.frameRange->lastFrame) + " (inclusive)" : "Entire timeline");
    }));
#define AUDIO_ASSET(member, expression) put("AudioAsset." member, records(d.audioAssets, [](const auto &a) { return expression; }))
    AUDIO_ASSET("id", text(a.id)); AUDIO_ASSET("sampleRate", text(a.id) + " · " + number(a.sampleRate) + " Hz");
    AUDIO_ASSET("channelCount", text(a.id) + " · " + number(a.channelCount));
    AUDIO_ASSET("samples.count", text(a.id) + " · " + number(a.samples.size()));
    AUDIO_ASSET("derivedDuration", text(a.id) + " · " + (a.sampleRate && a.channelCount ? seconds(double(a.samples.size()) / a.sampleRate / a.channelCount) : unavailable));
#undef AUDIO_ASSET
#define TRACK(member, format) put("AudioTrackLayer." #member, records(d.audioTracks, [](const auto &t) { return format(t.member); }))
    TRACK(id, text); TRACK(name, text); TRACK(muted, boolean);
    put("AudioTrackLayer.gainDb", records(d.audioTracks, [](const auto &t) { return number(t.gainDb) + " dB"; }));
#undef TRACK
#define CLIP(member, format) put("AudioClip." #member, records(d.audioTracks, [](const auto &t) { return text(t.id) + "\n" + records(t.clips, [](const auto &c) { return format(c.member); }); }))
    CLIP(id, text); CLIP(name, text); CLIP(assetId, text); CLIP(startFrame, number);
    CLIP(durationFrames, number); CLIP(sourceOffsetSamples, number); CLIP(enabled, boolean);
    put("AudioClip.gainDb", records(d.audioTracks, [](const auto &t) { return text(t.id) + "\n" + records(t.clips, [](const auto &c) { return number(c.gainDb) + " dB"; }); }));
#undef CLIP

    put("EditorCanvas.documentReady", boolean(documentReady()));
    put("EditorProject.modified", boolean(documentReady() && (filePath().isEmpty() || !documentSynchronized())));
    put("EditorCanvas.documentSynchronized", boolean(documentSynchronized()));
    put("Alias.exists", alias("exists")); put("Alias.available", alias("available"));
    put("DocumentFile.revision", filePath().isEmpty() ? QStringLiteral("Not applicable") : m_fileStoredRevision);
    put("EditorCanvas.revision", number(revision()));
    put("EditorCanvas.filePath", text(filePath()));
    put("EditorCanvas.fileAlias", m_documentAlias.empty() ? "Not associated" : "Associated");
    for (const auto &key : {"metadata2", "containerId", "id", "key", "section", "relativePath", "folderPath", "source", "version", "history", "path", "previewSource", "thumbnailSource"})
        put(qPrintable("Alias." + QString::fromLatin1(key)), alias(key));
    put("Alias.path/EditorCanvas.filePath/DocumentFile.filePath", alias("path"));
    put("Alias.containerId/Alias.key", alias("containerId") + " / " + alias("key"));
    put("EditorCanvas.error", error().isEmpty() ? "No errors" : error());
    put("FileSystem.readCanvas.source", alias("source"));
    if (!documentReady()) {
        for (auto i = values.begin(); i != values.end(); ++i) {
            const auto prefix = i.key().section('.', 0, 0);
            if (prefix != "EditorCanvas" && prefix != "EditorProject" && prefix != "Alias" && prefix != "FileSystem" && i.key() != "Derived.storageFormat")
                i.value() = unavailable;
        }
    }
    return values;
}

QVariantMap EditorProject::fileInformation() const
{
    auto values = currentCanvas()->fileInformation();
    values["EditorProject.modified"] = boolean(modified());
    values["EditorProject.canvasCount"] = number(canvasCount());
    values["EditorProject.currentIndex"] = number(currentIndex());
    values["EditorCanvas.error"] = error().isEmpty() ? QStringLiteral("No errors") : error();
    return values;
}
