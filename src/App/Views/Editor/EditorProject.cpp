#include "EditorProject.h"
#include "EditorProjectCodec.h"
#include "EditorToolUtils.h"
#include <iiFileProvider.h>
#include <iiSharedCanvas/Media/MediaIo.h>
#include <QFileInfo>
#include <QSet>
#include <QQmlEngine>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
using namespace iiSharedCanvas;
QString pathFor(const QUrl &url) { return url.isLocalFile() ? url.toLocalFile() : url.scheme().isEmpty() ? url.toString() : QString{}; }
// Import into one asset pool without flattening layers or losing keyframes.
void appendArtboards(Document &target, Document source, const std::string &name, int number, std::int64_t &x) {
    if (source.canvasMode != CanvasMode::Finite) throw std::runtime_error("Attach finite image canvases; open infinite documents directly.");
    const auto view = documentViewRegion(source);
    if (view.extent.width <= 0 || x + view.extent.width > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("The horizontal artboard workspace exceeds its geometry limit.");
    if (number == 0) { target.extent = source.extent; target.timeline = source.timeline; target.stableDiffusionMetadata = source.stableDiffusionMetadata; }
    else if (std::uint64_t(target.timeline.frameRate.numerator) * source.timeline.frameRate.denominator != std::uint64_t(source.timeline.frameRate.numerator) * target.timeline.frameRate.denominator) throw std::runtime_error("Attached canvases must use the same timeline frame rate.");
    target.timeline.frameCount = std::max(target.timeline.frameCount, source.timeline.frameCount);
    const auto prefix = "attachment." + std::to_string(number) + ":";
    const auto looseBoard = prefix + "canvas";
    const bool hasLoose = source.artboards.empty() || std::ranges::any_of(source.layers, [](const Layer &layer) { return !layerProperties(layer).artboardId; });
    if (hasLoose) target.artboards.push_back({looseBoard, name, {{int(x), 0}, view.extent}, 0});
    for (auto board : source.artboards) {
        board.id = prefix + board.id;
        board.region.origin.x += int(x) - view.origin.x; board.region.origin.y -= view.origin.y;
        target.artboards.push_back(std::move(board));
    }
    for (auto asset : source.assets) { std::visit([&](auto &value) { value.id = prefix + value.id; }, asset); target.assets.push_back(std::move(asset)); }
    for (auto layer : source.layers) {
        auto &p = layerProperties(layer); p.id = prefix + p.id;
        if (p.artboardId) p.artboardId = prefix + *p.artboardId;
        else { p.artboardId = looseBoard; p.transform.translationX -= view.origin.x; p.transform.translationY -= view.origin.y; }
        auto sourceValue = layerSource(layer);
        if (auto *fixed = std::get_if<StaticSource>(&sourceValue)) fixed->assetId = prefix + fixed->assetId;
        setLayerSource(layer, std::move(sourceValue)); target.layers.push_back(std::move(layer));
    }
    for (auto frame : source.frames) {
        for (auto &key : frame.keyframes) { key.layerId = prefix + key.layerId; key.assetId = prefix + key.assetId; }
        auto existing = std::ranges::find(target.frames, frame.index, &Frame::index);
        if (existing == target.frames.end()) target.frames.push_back(std::move(frame));
        else existing->keyframes.insert(existing->keyframes.end(), frame.keyframes.begin(), frame.keyframes.end());
    }
    std::ranges::sort(target.frames, {}, &Frame::index);
    for (auto asset : source.audioAssets) { asset.id = prefix + asset.id; target.audioAssets.push_back(std::move(asset)); }
    for (auto track : source.audioTracks) {
        track.id = prefix + track.id;
        for (auto &clip : track.clips) { clip.id = prefix + clip.id; clip.assetId = prefix + clip.assetId; }
        target.audioTracks.push_back(std::move(track));
    }
    x += view.extent.width + 64;
    recordDocumentChange(target);
}
}
EditorProject::EditorProject(QObject *parent) : QObject(parent) {
    connect(this, &EditorProject::projectChanged, this, &EditorProject::fileInformationChanged);
    connect(this, &EditorProject::errorChanged, this, &EditorProject::fileInformationChanged);
    replace(std::make_unique<EditorCanvas>());
}
EditorProject::~EditorProject() { disconnect(m_canvas.get(), nullptr, this, nullptr); }
int EditorProject::canvasCount() const { return m_canvas->documentReady() ? std::max(1, int(m_canvas->document()->artboards.size())) : 1; }
QVariantList EditorProject::artboards() const {
    QVariantList result;
    if (!m_canvas->documentReady()) return result;
    for (const auto &board : m_canvas->document()->artboards) result.append(QVariantMap{
        {"id", QString::fromStdString(board.id)}, {"name", QString::fromStdString(board.name)},
        {"x", board.region.origin.x}, {"y", board.region.origin.y},
        {"width", board.region.extent.width}, {"height", board.region.extent.height},
        {"selected", int(result.size()) == currentIndex()}});
    return result;
}
void EditorProject::invalidatePagePreviews(bool force) {
    const auto revision = m_canvas->revision();
    const auto frame = m_canvas->frame();
    const bool ready = m_canvas->documentReady();
    if (force || !m_previewStateValid || revision != m_lastPreviewCanvasRevision
        || frame != m_lastPreviewFrame || ready != m_lastPreviewDocumentReady) {
        m_pagePreviews.clear();
        m_lastPreviewCanvasRevision = revision;
        m_lastPreviewFrame = frame;
        m_lastPreviewDocumentReady = ready;
        m_previewStateValid = true;
        m_previewNotificationPending = true;
    }
    // Direct preview requests still use the current native revision. Only
    // presentation bindings wait until the live stroke ends or is cancelled.
    if (!m_previewNotificationPending || m_canvas->liveStrokeActive()) return;
    m_previewNotificationPending = false;
    ++m_pagePreviewRevision;
    emit pagePreviewsChanged();
}
QUrl EditorProject::pagePreviewSource(int index, int maxWidth, int maxHeight) {
    if (!m_canvas->documentReady() || m_canvas->infiniteCanvas() || index < 0 || index >= canvasCount()) return {};
    const auto *document = m_canvas->document();
    const auto &boards = document->artboards;
    const auto extent = boards.empty() ? documentViewRegion(*document).extent : boards[index].region.extent;
    if (extent.width <= 0 || extent.height <= 0) return {};
    const auto scale = std::min(double(std::clamp(maxWidth, 1, 1024)) / extent.width,
                                double(std::clamp(maxHeight, 1, 1024)) / extent.height);
    const CanvasExtent output{std::max(1, int(std::floor(extent.width * scale))),
                              std::max(1, int(std::floor(extent.height * scale)))};
    const auto key = QString::number(index) + ':' + QString::number(m_canvas->revision()) + ':'
        + QString::number(m_canvas->frame()) + ':' + QString::number(output.width) + 'x' + QString::number(output.height);
    if (const auto *cached = m_pagePreviews.object(key)) return *cached;
    try {
        const auto rendered = boards.empty()
            ? renderFrameRegion(*document, m_canvas->frame(), documentViewRegion(*document), output, RasterSampling::Smooth)
            : renderArtboard(*document, m_canvas->frame(), boards[index].id, output, RasterSampling::Smooth);
        if (!rendered.ok()) return {};
        const auto source = editorTools::dataImage(editorTools::image(rendered.pixels));
        if (!source.isEmpty()) m_pagePreviews.insert(key, new QUrl(source));
        return source;
    } catch (const std::exception &) {
        // Optional presentation must not change the document's editing error.
        return {};
    }
}
void EditorProject::attachCanvas(QQuickItem *surface) {
    if (!surface) return;
    const bool resized = m_canvas->size() != surface->size();
    m_canvas->setSmoothRendering(true); m_canvas->setParentItem(surface); m_canvas->setSize(surface->size());
    if (resized && m_canvas->documentReady()) m_canvas->fitToView();
}
QString EditorProject::filePath() const { return m_canvas->filePath(); }
QString EditorProject::documentName() const { return m_canvas->documentName(); }
bool EditorProject::fail(const QString &message) { m_error = message; emit errorChanged(); return false; }
void EditorProject::replace(std::unique_ptr<EditorCanvas> canvas, int index) {
    if (m_canvas) { m_canvas->setVisible(false); m_canvas->setParentItem(nullptr); disconnect(m_canvas.get(), nullptr, this, nullptr); }
    auto previous = std::move(m_canvas);
    m_canvas = std::move(canvas); m_error.clear();
    invalidatePagePreviews(true);
    m_canvas->setParent(this); QQmlEngine::setObjectOwnership(m_canvas.get(), QQmlEngine::CppOwnership);
    m_canvas->setObjectName("editorBlankCanvas"); m_canvas->setVisible(true);
    if (m_canvas->documentReady() && !m_canvas->document()->artboards.empty()) m_canvas->selectArtboard(index, false);
    m_modified = m_canvas->filePath().isEmpty() || !m_canvas->documentSynchronized();
    connect(m_canvas.get(), &EditorCanvas::errorChanged, this, &EditorProject::errorChanged);
    const auto invalidatePreviews = [this] { invalidatePagePreviews(); };
    connect(m_canvas.get(), &EditorCanvas::revisionChanged, this, invalidatePreviews);
    connect(m_canvas.get(), &EditorCanvas::frameChanged, this, invalidatePreviews);
    connect(m_canvas.get(), &EditorCanvas::documentChanged, this, invalidatePreviews);
    connect(m_canvas.get(), &EditorCanvas::liveStrokeActiveChanged, this, invalidatePreviews);
    connect(m_canvas.get(), &EditorCanvas::documentInfoChanged, this, &EditorProject::projectChanged);
    connect(m_canvas.get(), &EditorCanvas::pagePresentationChanged, this, &EditorProject::fileInformationChanged);
    connect(m_canvas.get(), &EditorCanvas::specificationChanged, this, &EditorProject::fileInformationChanged);
    connect(m_canvas.get(), &EditorCanvas::activeArtboardChanged, this, [this] { emit currentCanvasChanged(); emit projectChanged(); });
    connect(m_canvas.get(), &EditorCanvas::revisionChanged, this, [this] { m_modified = m_canvas->filePath().isEmpty() || !m_canvas->documentSynchronized(); emit projectChanged(); });
    emit currentCanvasChanged(); emit projectChanged(); emit errorChanged();
}
bool EditorProject::setCurrentIndex(int index) {
    if (index < 0 || index >= canvasCount()) return fail(tr("Choose an existing artboard."));
    if (!m_canvas->selectArtboard(index)) return fail(m_canvas->error());
    m_error.clear(); emit errorChanged(); return true;
}
bool EditorProject::createCanvas(const QVariantMap &specification) {
    auto canvas = std::make_unique<EditorCanvas>();
    if (!canvas->createCanvas(specification)) return fail(canvas->error());
    replace(std::move(canvas)); return true;
}
bool EditorProject::openImages(const QVariantList &sources) {
    if (sources.isEmpty() || sources.size() > dreamscapes::MaximumProjectPages) return fail(tr("Select between 1 and 1000 images."));
    try {
        Document document; QSet<QString> paths; std::uint64_t pixels = 0; std::int64_t x = 0; int number = 0;
        for (const auto &source : sources) {
            const auto path = pathFor(source.toUrl());
            if (path.isEmpty()) return fail(tr("Choose local images."));
            const auto canonical = QFileInfo(path).canonicalFilePath();
            if (!canonical.isEmpty() && paths.contains(canonical)) continue;
            EditorCanvas imported;
            if (!imported.openDocumentSource(source.toUrl(), true)) return fail(imported.error());
            pixels += std::uint64_t(imported.canvasWidth()) * imported.canvasHeight();
            if (pixels > MediaLimits{}.maxPixelsPerFrame) return fail(tr("Selected images exceed the project import limit."));
            appendArtboards(document, *imported.document(), QFileInfo(path).fileName().toStdString(), number++, x); paths.insert(canonical);
        }
        if (document.artboards.size() > dreamscapes::MaximumProjectPages) return fail(tr("The project supports at most 1000 artboards."));
        auto canvas = std::make_unique<EditorCanvas>();
        if (!canvas->loadProjectPage(std::move(document), tr("Image Project"))) return fail(canvas->error());
        replace(std::move(canvas)); return true;
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool EditorProject::attachImages(const QVariantList &sources) {
    if (!m_canvas->documentReady()) return openImages(sources);
    if (sources.isEmpty() || sources.size() > dreamscapes::MaximumProjectPages) return fail(tr("Select between 1 and 1000 images."));
    if (m_canvas->liveStrokeActive()) return fail(tr("Finish the current stroke before attaching images."));
    if (m_canvas->infiniteCanvas()) return fail(tr("Attach artboards to a finite project."));
    try {
        auto document = *m_canvas->document();
        if (document.artboards.empty()) {
            const auto region = documentViewRegion(document);
            document.artboards.push_back({"project.canvas", m_canvas->documentName().toStdString(), region, 0});
            for (auto &layer : document.layers) {
                auto &properties = layerProperties(layer); properties.artboardId = "project.canvas";
                properties.transform.translationX -= region.origin.x; properties.transform.translationY -= region.origin.y;
            }
        }
        const auto firstNew = int(document.artboards.size());
        const auto view = documentViewRegion(document);
        std::int64_t x = std::int64_t(view.origin.x) + view.extent.width + 64;
        std::uint64_t pixels = 0;
        for (const auto &board : document.artboards) pixels += std::uint64_t(board.region.extent.width) * board.region.extent.height;
        int number = firstNew; QSet<QString> paths;
        for (const auto &source : sources) {
            const auto path = pathFor(source.toUrl());
            if (path.isEmpty()) return fail(tr("Choose local images."));
            const auto canonical = QFileInfo(path).canonicalFilePath();
            if (!canonical.isEmpty() && paths.contains(canonical)) continue;
            EditorCanvas imported;
            if (!imported.openDocumentSource(source.toUrl(), true)) return fail(imported.error());
            pixels += std::uint64_t(imported.canvasWidth()) * imported.canvasHeight();
            if (pixels > MediaLimits{}.maxPixelsPerFrame) return fail(tr("Selected images exceed the project import limit."));
            for (;;) {
                const auto prefix = "attachment." + std::to_string(number) + ":";
                const bool collision = std::ranges::any_of(document.assets, [&](const Asset &a) { return assetId(a).starts_with(prefix); })
                    || std::ranges::any_of(document.artboards, [&](const Artboard &b) { return b.id.starts_with(prefix); })
                    || std::ranges::any_of(document.layers, [&](const Layer &l) { return layerProperties(l).id.starts_with(prefix); });
                if (!collision) break;
                ++number;
            }
            appendArtboards(document, *imported.document(), QFileInfo(path).fileName().toStdString(), number++, x);
            paths.insert(canonical);
        }
        if (document.artboards.size() > dreamscapes::MaximumProjectPages) return fail(tr("The project supports at most 1000 artboards."));
        if (!m_canvas->applyArtboardImport(std::move(document))) return fail(m_canvas->error());
        m_canvas->selectArtboard(firstNew, false); m_canvas->fitToView(); return setCurrentIndex(firstNew);
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool EditorProject::openDocumentSource(const QUrl &source, bool asCopy) {
    const auto path = pathFor(source);
    if (path.isEmpty()) return fail(tr("Choose a local canvas or project."));
    if (QFileInfo(path).suffix().compare("iiscp", Qt::CaseInsensitive) == 0) {
        try {
            const auto bytes = iiFileProvider::File::read(path, SerializationLimits{}.maximumContainerBytes);
            auto decoded = dreamscapes::decodeProject({reinterpret_cast<const std::uint8_t *>(bytes.constData()), std::size_t(bytes.size())});
            if (!decoded.error.empty()) return fail(QString::fromStdString(decoded.error));
            Document document; std::int64_t x = 0; int number = 0;
            for (auto &page : decoded.pages) appendArtboards(document, std::move(page.document), page.name, number++, x);
            auto canvas = std::make_unique<EditorCanvas>();
            if (!canvas->loadProjectPage(std::move(document), QFileInfo(path).completeBaseName())) return fail(canvas->error());
            replace(std::move(canvas), decoded.active); return true;
        } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
    }
    auto canvas = std::make_unique<EditorCanvas>();
    if (!canvas->openDocumentSource(source, asCopy)) return fail(canvas->error());
    replace(std::move(canvas)); return true;
}
bool EditorProject::saveDocumentAs(const QUrl &destination) {
    if (!m_canvas->saveDocumentAs(destination)) return fail(m_canvas->error());
    m_modified = false; m_error.clear(); emit projectChanged(); emit errorChanged(); return true;
}
bool EditorProject::saveDocument() {
    if (filePath().isEmpty()) return fail(tr("Choose an .iisc path with Save As."));
    return saveDocumentAs(QUrl::fromLocalFile(filePath()));
}
