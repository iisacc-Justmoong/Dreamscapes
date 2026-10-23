#include "EditorPhotoImportCommand.h"
#include "EditorCanvas.h"
#include "EditorDocumentOpen.h"
#include "EditorMedia.h"
#include "EditorFrameExporter.h"
#include "EditorColorSampler.h"
#include <QQmlEngine>
#include "EditorCameraPhoto.h"
#include "EditorToolUtils.h"
#include "EditorStillTiming.h"
#include "App/Generation/ImageCanvas.h"
#include <iiSharedCanvas/Bitmap/BitmapCodec.h>
#include <iiSharedCanvas/Validation/Validation.h>
#include <iiFileProvider.h>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QUuid>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>

using namespace iiSharedCanvas;
namespace {
using dreamscapes::assignDefaultStillRanges;
QString localPath(const QUrl &url) {
    if (url.isLocalFile()) return url.toLocalFile();
    return url.scheme().isEmpty() ? url.toString() : QString{};
}
QVariantMap layerDescription(const Layer &layer, const QString &selected) {
    const auto &p = layerProperties(layer);
    const auto kind = contentKind(layer);
    const QString type = kind == ContentKind::Video ? QStringLiteral("Video")
        : kind == ContentKind::Vector ? QStringLiteral("Vector")
        : kind == ContentKind::IpAdapter ? QStringLiteral("Embedding") : QStringLiteral("Bitmap");
    return {{"id", QString::fromStdString(p.id)}, {"name", QString::fromStdString(p.name)},
        {"visible", p.visible}, {"opacity", p.opacity}, {"type", type},
        {"selected", QString::fromStdString(p.id) == selected},
        {"x", p.transform.translationX}, {"y", p.transform.translationY},
        {"blend", p.blendMode == RasterBlendMode::Multiply ? "Multiply" : p.blendMode == RasterBlendMode::Screen ? "Screen" : p.blendMode == RasterBlendMode::Overlay ? "Overlay" : "Normal"}};
}
}

EditorCanvas::EditorCanvas(QQuickItem *parent):EditorCanvas(std::make_shared<EditorFrameExporter>(),parent) {}
EditorCanvas::EditorCanvas(std::shared_ptr<EditorFrameExporter> exporter,QQuickItem *parent)
    : EditorCanvas(std::move(exporter), std::make_unique<EditorColorSampler>(), parent) {}
EditorCanvas::EditorCanvas(std::shared_ptr<EditorFrameExporter> exporter, std::unique_ptr<EditorColorSampler> sampler, QQuickItem *parent)
    : CanvasItem(parent), m_media(std::make_unique<EditorMedia>(this)),
      m_frameExporter(exporter?std::move(exporter):std::make_shared<EditorFrameExporter>()),
      m_colorSampler(sampler ? std::move(sampler) : std::make_unique<EditorColorSampler>()),
      m_photoSources(std::make_unique<EditorCameraPhoto>(this)) {
    m_documentStorage.setObjectName("editorDocumentStorage");
    QQmlEngine::setObjectOwnership(m_frameExporter.get(),QQmlEngine::CppOwnership);
    connect(m_colorSampler.get(), &EditorColorSampler::finished, this, &EditorCanvas::finishColorRead);
    const auto retireRead = [this] { if (retireColorRead()) emit toolStateChanged(); };
    connect(this, &CanvasItem::documentChanged, this, retireRead);
    connect(this, &CanvasItem::revisionChanged, this, retireRead);
    connect(this, &CanvasItem::liveStrokeActiveChanged, this, [this, retireRead] { if (liveStrokeActive()) retireRead(); });
    const auto retireNeutral = [this, retireRead] { if (m_colorRead == ColorRead::Neutral) retireRead(); };
    connect(this, &CanvasItem::frameChanged, this, retireNeutral);
    connect(this, &CanvasItem::selectionChanged, this, retireNeutral);
    connect(this, &EditorCanvas::activeArtboardChanged, this, retireNeutral);
    m_viewOptions = std::make_unique<EditorViewOptions>(this);
    connect(m_viewOptions.get(), &EditorViewOptions::changed, this, [this] { resetAlignmentGesture(); });
    connect(m_viewOptions.get(), &EditorViewOptions::guidesChanged, this, &EditorCanvas::resetAlignmentGesture);
    connect(this, &EditorCanvas::documentInfoChanged, this, [this] {
        if(m_elementTransformHandle.isEmpty() && !m_creatingElement) resetAlignmentGesture();
    });
    setAcceptHoverEvents(true);
    connect(this, &CanvasItem::viewportChanged, this, &EditorCanvas::notifyInteractionFeedback);
    connect(this, &CanvasItem::brushChanged, this, &EditorCanvas::notifyInteractionFeedback);
    connect(this, &EditorCanvas::toolStateChanged, this, &EditorCanvas::notifyInteractionFeedback);
    connect(this, &EditorCanvas::elementTransformChanged, this, &EditorCanvas::notifyInteractionFeedback);
    connect(this, &EditorCanvas::maskPanelChanged, this, &EditorCanvas::notifyInteractionFeedback);
    connect(this, &CanvasItem::frameChanged, this, &EditorCanvas::cancelInteractionGesture);
    connect(this, &EditorCanvas::activeArtboardChanged, this, &EditorCanvas::cancelInteractionGesture);
    connect(this, &QQuickItem::visibleChanged, this, [this] {
        if (!isVisible()) { cancelInteractionGesture(); m_feedbackPointerInside = false; notifyInteractionFeedback(); }
    });
    connect(this, &EditorCanvas::elementSelectionChanged, this, &EditorCanvas::elementTransformChanged);
    connect(this, &CanvasItem::viewportChanged, this, &EditorCanvas::elementTransformChanged);
    connect(this, &CanvasItem::frameChanged, this, &EditorCanvas::cancelElementTransform);
    connect(this, &CanvasItem::frameChanged, this, &EditorCanvas::elementTransformChanged);
    connect(this, &EditorCanvas::toolStateChanged, this, &EditorCanvas::elementTransformChanged);
    m_photoImport = std::make_unique<EditorPhotoImportCommand>(this, m_photoSources.get(), this);
    connect(this, &CanvasItem::viewportChanged, this, &EditorCanvas::constrainViewport);
    connect(this, &CanvasItem::documentChanged, this, &EditorCanvas::constrainViewport);
    connect(m_media.get(), &EditorMedia::captured, this, [this](const QImage &image) {
        auto captured = image; const auto values = settingsFor("camera-photo");
        if (values.value("selector").toString() == "Document Scan") {
            ColorAdjustment a; const auto mode = editorTools::text(values, 9, "Color");
            a.saturation = mode == "Color" ? 0 : -1; a.contrast = editorTools::number(values, 10, 68) / 100;
            const auto result = adjustRaster(editorTools::raster(image), a);
            if (!result.ok()) { fail(QString::fromStdString(result.error)); return; }
            captured = editorTools::image(result.pixels);
            if (mode == "B&W") for (int y = 0; y < captured.height(); ++y) for (int x = 0; x < captured.width(); ++x) captured.setPixel(x, y, qGray(captured.pixel(x, y)) > 128 ? 0xffffffff : 0xff000000);
        }
        insertPixels(captured, tr("Camera capture"));
    });
    connect(m_media.get(), &EditorMedia::cameraChanged, this, &EditorCanvas::toolStateChanged);
    connect(this, &CanvasItem::selectionChanged, this, [this] { invalidateToolPreview(); emit toolStateChanged(); });
    connect(this, &CanvasItem::revisionChanged, this, &EditorCanvas::toolStateChanged);
    connect(this, &CanvasItem::revisionChanged, this, &EditorCanvas::recordHistory);
    connect(this, &EditorCanvas::toolStateChanged, this, &EditorCanvas::refreshBackgroundMaskOverlay);
    connect(this, &EditorCanvas::documentInfoChanged, this, &EditorCanvas::refreshBackgroundMaskOverlay);
    connect(this, &CanvasItem::selectionChanged, this, [this] {
        if(m_maskGestureActive && m_maskGestureTarget!=selectedLayerId()) cancelBackgroundMaskGesture();
        m_maskPolygon.clear();
    });
    connect(this, &CanvasItem::documentChanged, this, &EditorCanvas::documentInfoChanged);
    connect(this, &CanvasItem::revisionChanged, this, &EditorCanvas::documentInfoChanged);
    // Presentation has its own notifications. Selection and seeking must not
    // invalidate file metadata, document structure, or project ownership.
    connect(this, &EditorCanvas::documentInfoChanged, this, &EditorCanvas::layerStateChanged);
    connect(this, &CanvasItem::selectionChanged, this, &EditorCanvas::layerStateChanged);
    connect(this, &CanvasItem::frameChanged, this, &EditorCanvas::layerStateChanged);
    connect(this, &EditorCanvas::activeArtboardChanged, this, &EditorCanvas::layerStateChanged);
    connect(this, &EditorCanvas::fileStateChanged, this, &EditorCanvas::layerStateChanged);
    connect(this, &EditorCanvas::documentInfoChanged, this, &EditorCanvas::editingGeometryChanged);
    connect(this, &EditorCanvas::activeArtboardChanged, this, &EditorCanvas::editingGeometryChanged);
    connect(this, &EditorCanvas::documentInfoChanged, this, &EditorCanvas::fileInformationChanged);
    connect(this, &EditorCanvas::fileStateChanged, this, &EditorCanvas::fileInformationChanged);
    connect(this, &EditorCanvas::specificationChanged, this, &EditorCanvas::fileInformationChanged);
    connect(this, &EditorCanvas::specificationChanged, this, &EditorCanvas::fileStateChanged);
    connect(this, &EditorCanvas::errorChanged, this, &EditorCanvas::fileInformationChanged);
    connect(this, &CanvasItem::frameChanged, this, &EditorCanvas::fileViewInformationChanged);
    connect(this, &EditorCanvas::editingGeometryChanged, this, &EditorCanvas::fileViewInformationChanged);
    connect(this, &EditorCanvas::pagePresentationChanged, this, &EditorCanvas::fileViewInformationChanged);
    connect(this, &CanvasItem::frameChanged, this, &EditorCanvas::refreshBackgroundMaskOverlay);
    const auto synchronize = [this] {
        if (!m_historySuspended && !m_synchronizingDocument && !m_documentAlias.isEmpty()
            && documentReady() && !liveStrokeActive()) synchronizeDocument(*document());
    };
    connect(this, &CanvasItem::revisionChanged, this, synchronize);
    connect(this, &CanvasItem::liveStrokeActiveChanged, this, synchronize);
    connect(m_documentStorage.container(), &iiSocietyContainer::Container::stateChanged, this,
        [this, revision = m_documentStorage.container()->revision()]() mutable {
        if (revision == m_documentStorage.container()->revision()) return;
        revision = m_documentStorage.container()->revision();
        if (m_synchronizingDocument || m_documentAlias.value("key").toString().isEmpty()) return;
        const auto state = m_documentStorage.container()->containerId() == m_documentAlias.value("containerId").toString()
            ? m_documentStorage.container()->alias(m_documentAlias.value("key").toString()) : QVariantMap{};
        const auto exists = state.value("exists", false), available = state.value("available", false);
        if (m_documentAlias.value("exists") == exists && m_documentAlias.value("available") == available) return;
        m_documentAlias["exists"] = exists;
        m_documentAlias["available"] = available;
        emit fileStateChanged();
    });
    connect(this, &CanvasItem::lastErrorChanged, this, &EditorCanvas::errorChanged);
    connect(this, &CanvasItem::selectionChanged, this, [this] { synchronizeElementSelection(true); });
    connect(this, &EditorCanvas::documentInfoChanged, this, [this] { synchronizeElementSelection(false); });
    connect(this, &EditorCanvas::activeArtboardChanged, this, [this] { synchronizeElementSelection(true); });
    connect(this, &CanvasItem::liveStrokeActiveChanged, this, &EditorCanvas::elementSelectionChanged);
    connect(this, &EditorCanvas::toolStateChanged, this, [this] {
        if (m_changingElementSelection || m_elementObservedTool == m_tool) return;
        m_elementObservedTool = m_tool;
        synchronizeElementSelection(true);
    });
    // Parenting sends ChildAdded synchronously. Derived interaction state must
    // already be initialized before a value-owned QObject enters this tree.
    m_documentStorage.setParent(this);
}
EditorCanvas::~EditorCanvas() { m_photoImport.reset(); m_colorSampler->cancel(); disconnect(m_colorSampler.get(), nullptr, this, nullptr); m_documentStorage.setParent(nullptr); unbind(); }
QUrl EditorCanvas::localDirectory(const QString &path) const {
    const auto value = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    return QFileInfo(value).isDir() ? QUrl::fromLocalFile(QFileInfo(value).absoluteFilePath()) : QUrl{};
}
QString EditorCanvas::error() const { return m_error.isEmpty() ? lastError() : m_error; }
bool EditorCanvas::fail(const QString &message) {
    m_error = message;
    emit errorChanged();
    return false;
}
void EditorCanvas::clearError() {
    if (m_error.isEmpty()) return;
    m_error.clear();
    emit errorChanged();
}
QString EditorCanvas::documentName() const {
    return filePath().isEmpty() ? m_specification.value("name", tr("Untitled Canvas")).toString()
                              : QFileInfo(filePath()).fileName();
}
QVariantList EditorCanvas::layers() const {
    QVariantList result;
    if (!documentReady()) return result;
    for (auto it = document()->layers.crbegin(); it != document()->layers.crend(); ++it)
        if (belongsToActiveArtboard(*it)) result.append(layerDescription(*it, selectedLayerId()));
    return result;
}
QVariantMap EditorCanvas::selectedLayer() const {
    if (!documentReady()) return {};
    const auto id = selectedLayerId();
    if (id.isEmpty()) return {};
    for (auto it = document()->layers.crbegin(); it != document()->layers.crend(); ++it) {
        if (QString::fromStdString(layerProperties(*it).id) != id || !belongsToActiveArtboard(*it)) continue;
        auto result = layerDescription(*it, id);
        result["lock"] = m_pixelLocks.contains(id) ? "Pixels" : m_positionLocks.contains(id) ? "Position" : "None";
        result["preserveAlpha"] = alphaProtected(id);
        return result;
    }
    return {};
}
QString EditorCanvas::selectedLayerId() const {
    return m_selectedDocumentLayer.isEmpty() ? CanvasItem::selectedLayerId() : m_selectedDocumentLayer;
}
bool EditorCanvas::selectLayer(const QString &id) {
    if (id != selectedLayerId()) cancelElementTransform();
    if (!documentReady()) return fail(tr("Open a canvas first."));
    const auto *layer = findLayer(*document(), id.toStdString());
    if (!layer) return fail(tr("Select an existing layer."));
    const bool selectingElements = m_changingElementSelection;
    QScopedValueRollback<bool> guard(m_changingElementSelection, true);
    if (layerProperties(*layer).artboardId && layerProperties(*layer).artboardId != m_activeArtboard) {
        const auto &boards = document()->artboards;
        for (int i = 0; i < int(boards.size()); ++i) if (boards[i].id == *layerProperties(*layer).artboardId) { selectArtboard(i, false); break; }
    }
    const auto *asset = resolveAssetAt(*document(), *layer, frame());
    if ((std::holds_alternative<StaticBitmapLayer>(*layer) || std::holds_alternative<DynamicBitmapLayer>(*layer))
        && asset && contentKind(*asset) == ContentKind::Raster) {
        const auto previous = m_selectedDocumentLayer;
        m_selectedDocumentLayer.clear();
        if (!CanvasItem::selectLayer(id)) {
            m_selectedDocumentLayer = previous;
            return fail(lastError());
        }
        if (!previous.isEmpty()) emit selectionChanged();
    } else {
        // CanvasItem's input selection is raster-only. The consumer also selects
        // native vectors/video/embeddings for document property editing.
        // CanvasItem::cancelStroke refreshes document content even without a
        // stroke, so ordinary vector selection must not call it unconditionally.
        if (liveStrokeActive()) cancelStroke();
        CanvasItem::clearSelection();
        m_selectedDocumentLayer = id;
        emit selectionChanged();
    }
    clearError();
    // Avoid intermediate raster clear/select notifications replacing a multiple
    // Elements selection. Ordinary layer selection intentionally becomes singular.
    m_changingElementSelection = selectingElements;
    if (!selectingElements) synchronizeElementSelection(true);
    else restoreSelectedElementSettings();
    return true;
}
void EditorCanvas::clearSelection() {
    cancelElementTransform();
    const bool hadDocumentSelection = !m_selectedDocumentLayer.isEmpty();
    m_selectedDocumentLayer.clear();
    CanvasItem::clearSelection();
    if (hadDocumentSelection) emit selectionChanged();
    if (!m_changingElementSelection) {
        m_selectedElements.clear();
        emit elementSelectionChanged();
    }
}
bool EditorCanvas::adopt(Document candidate, const QVariantMap &specification) {
    m_viewOptions->clearGuides(); m_textBaselines.clear();
    cancelElementTransform();
    cancelInteractionGesture();
    const auto validation = validate(candidate);
    if (!validation.ok()) return fail(QString::fromStdString(validation.issues.front().message));
    retireColorRead(); m_colorReadError.clear();
    dreamscapes::normalizeImplicitStill(candidate);
    cancelStroke(); cancelBackgroundMaskGesture(); m_selectedBackgroundMask.clear();
    m_historySuspended = true;
    m_selectedDocumentLayer.clear(); m_activeArtboard.clear();
    ++m_graphicDocumentSequence; m_graphicMaskOperand.reset(); m_graphicMaskLayer.clear();
    m_layerColorPicking = m_layerColorValid = m_histogramShadows = m_histogramHighlights = false;
    clearAreaSelection(); m_elementBounds.clear(); m_elementSettings.clear(); m_elementPatterns.clear(); m_patternImage = {}; m_selectedElements.clear(); m_textOrigins.clear(); m_textSettings.clear(); m_audioSource = {}; m_audioTrack.clear(); m_media->stop();
    unbind();
    m_documentAlias.clear(); m_documentVersion.clear(); m_documentSynchronized = true;
    m_canvas = std::move(candidate);
    if (!bind(m_canvas)) { m_historySuspended = false; return fail(lastError()); }
    m_specification = specification;
    if (!m_canvas.artboards.empty()) selectArtboard(0, false);
    else if (!m_canvas.layers.empty()) selectLayer(QString::fromStdString(layerProperties(m_canvas.layers.back()).id));
    resetHistory();
    clearError();
    emit specificationChanged();
    emit documentInfoChanged();
    fitToView();
    return true;
}
void EditorCanvas::restoreView(qreal scale, qreal x, qreal y, quint32 currentFrame, const QString &selection) {
    setFrame(currentFrame);
    if (!selection.isEmpty()) selectLayer(selection);
    setZoom(scale);
    setPanX(x);
    setPanY(y);
}
bool EditorCanvas::openDocumentSource(const QUrl &source, bool asCopy) {
    const auto path = localPath(source);
    if (path.isEmpty()) return fail(tr("Choose a local canvas document or image."));
    return adoptDocumentOpen(dreamscapes::prepareDocumentOpen({std::filesystem::path(path.toStdU16String()),{}, {},asCopy}));
}
bool EditorCanvas::adoptDocumentOpen(dreamscapes::DocumentOpenResult result) {
    if(!result.ok())return fail(result.cancelled?tr("Opening was cancelled."):QString::fromStdString(result.error));
    if(!dreamscapes::documentSourceUnchanged(result))return fail(tr("The source document changed. Open it again."));
    if(result.bindFile&&!m_documentStorage.adoptStorage(result.storage))return fail(m_documentStorage.errorString());
    const auto extent=result.document.extent;const auto previousFrames=result.document.timeline.frameCount;
    if(!adopt(std::move(result.document),{{"name",QString::fromStdString(result.name)},{"pixelWidth",extent.width},{"pixelHeight",extent.height}}))return false;
    if(result.bindFile) {
        m_documentAlias=std::move(result.alias);m_documentVersion=m_documentAlias.value("version").toString();
        if(m_canvas.timeline.frameCount!=previousFrames&&!synchronizeDocument(m_canvas))return false;
        emit fileStateChanged();
    }
    return true;
}
bool EditorCanvas::openImages(const QVariantList &sources) {
    if (sources.isEmpty()) return fail(tr("Select at least one image."));
    if (sources.size() == 1 && QFileInfo(localPath(sources.first().toUrl())).suffix().compare("iisc", Qt::CaseInsensitive) == 0)
        return openDocumentSource(sources.first().toUrl(), true);
    try {
        Document candidate;
        QSet<QString> importedPaths;
        QString name;
        std::uint64_t remainingPixels = MediaLimits{}.maxPixelsPerFrame;
        for (const auto &source : sources) {
            const auto path = localPath(source.toUrl());
            if (path.isEmpty()) return fail(tr("Choose local images to open in the canvas."));
            const QFileInfo info(path);
            const auto canonical = info.canonicalFilePath();
            if (!canonical.isEmpty() && importedPaths.contains(canonical)) continue;
            if (remainingPixels == 0) return fail(tr("The selected images exceed the canvas import limit."));
            BitmapImportOptions options;
            options.assetId = "image.asset." + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            options.extendedCodecs = false;
            options.limits.maxPixelsPerFrame = remainingPixels;
            auto imported = importBitmap(path.toStdString(), options);
            if (!imported.ok()) return fail(QString::fromStdString(imported.result.message));
            QString wrapError;
            if (dreamscapes::persistImageCanvas(path, &wrapError).isEmpty()) return fail(wrapError);
            const auto &pixels = imported.asset.pixels;
            remainingPixels -= static_cast<std::uint64_t>(pixels.width) * pixels.height;
            candidate.extent.width = std::max(candidate.extent.width, pixels.width);
            candidate.extent.height = std::max(candidate.extent.height, pixels.height);
            const auto layerId = "image.layer." + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            candidate.layers.emplace_back(StaticBitmapLayer{{layerId, info.fileName().toStdString()}, StaticSource{imported.asset.id}});
            candidate.assets.emplace_back(std::move(imported.asset));
            importedPaths.insert(canonical);
            name = info.completeBaseName();
        }
        for (std::size_t index = 0; index < candidate.layers.size(); ++index) {
            auto &properties = layerProperties(candidate.layers[index]);
            const auto &pixels = std::get<RasterAsset>(candidate.assets[index]).pixels;
            properties.transform.translationX = (candidate.extent.width - pixels.width) / 2.0;
            properties.transform.translationY = (candidate.extent.height - pixels.height) / 2.0;
        }
        const auto extent = candidate.extent;
        const auto count = candidate.layers.size();
        if (count > 1) name = tr("Images (%1)").arg(count);
        return adopt(std::move(candidate), {{"name", name}, {"pixelWidth", extent.width}, {"pixelHeight", extent.height}, {"sourceCount", static_cast<int>(count)}});
    } catch (const std::exception &exception) { return fail(QString::fromUtf8(exception.what())); }
}
bool EditorCanvas::synchronizeDocument(const Document &document) {
    if (m_documentAlias.isEmpty()) return true;
    const auto encoded = encodeIisc(document);
    if (!encoded.ok()) return fail(QString::fromStdString(encoded.error.message));
    QScopedValueRollback<bool> guard(m_synchronizingDocument, true);
    const auto saved = m_documentStorage.saveCanvas(
        QByteArray(reinterpret_cast<const char *>(encoded.bytes.data()), qsizetype(encoded.bytes.size())), filePath(), m_documentVersion);
    m_documentSynchronized = !saved.isEmpty();
    if (saved.isEmpty()) { emit fileStateChanged(); return fail(m_documentStorage.errorString()); }
    m_documentAlias = saved; m_documentVersion = saved.value("version").toString();
    clearError(); emit fileStateChanged(); return true;
}
bool EditorCanvas::saveDocumentAs(const QUrl &destination) {
    cancelElementTransform();
    const auto path = localPath(destination);
    if (!documentReady() || path.isEmpty() || !path.endsWith(".iisc", Qt::CaseInsensitive))
        return fail(tr("Save the canvas to a local .iisc file."));
    if (liveStrokeActive()) return fail(tr("Finish the current stroke before saving."));
    if (path == filePath()) return synchronizeDocument(*document());
    m_documentStorage.openForSource(path);
    const auto encoded = encodeIisc(*document());
    if (!encoded.ok()) return fail(QString::fromStdString(encoded.error.message));
    const auto saved = m_documentStorage.saveCanvas(
        QByteArray(reinterpret_cast<const char *>(encoded.bytes.data()), qsizetype(encoded.bytes.size())), path);
    if (saved.isEmpty()) return fail(m_documentStorage.errorString());
    m_documentAlias = saved; m_documentVersion = saved.value("version").toString(); m_documentSynchronized = true;
    clearError(); emit fileStateChanged(); return true;
}
bool EditorCanvas::saveDocument() { return saveDocumentAs(QUrl::fromLocalFile(filePath())); }
DocumentEditResult EditorCanvas::editDocument(const std::function<DocumentEditResult(DocumentEditor &)> &edit) {
    cancelElementTransform();
    if (!documentReady() || !edit) return {DocumentEditCode::NotBound, false, "document", "Open a canvas first."};
    auto candidate = *document(); DocumentEditor editor(candidate);
    const auto result = edit(editor);
    if (!result.ok()) { fail(QString::fromStdString(result.message)); return result; }
    if (!result.changed) return result;
    QScopedValueRollback<bool> guard(m_synchronizingDocument, true);
    if (!synchronizeDocument(candidate)) return {DocumentEditCode::PersistenceFailed, false, "document", error().toStdString()};
    m_canvas = std::move(candidate); refresh(); return result;
}
bool EditorCanvas::setPublicationMetadata(PublicationMetadata metadata) {
    if (liveStrokeActive()) return fail(tr("Finish the current stroke before saving publication metadata."));
    return editDocument([&](DocumentEditor &editor) {
        return editor.setPublicationMetadata(std::move(metadata));
    }).ok();
}
bool EditorCanvas::commit(const std::function<bool(Document &)> &edit) {
    cancelElementTransform();
    if (!documentReady()) return fail(tr("Open or create a canvas first."));
    if (liveStrokeActive()) return fail(tr("Finish the current stroke before editing the document."));
    try {
        const auto scopedEdit = [&](Document &draft) {
            QSet<QString> previous;
            for (const auto &layer : draft.layers) previous.insert(QString::fromStdString(layerProperties(layer).id));
            if (!edit(draft)) return false;
            assignDefaultStillRanges(draft, [&](const Layer &layer) {
                return !previous.contains(QString::fromStdString(layerProperties(layer).id));
            });
            if (activeArtboard()) for (auto &layer : draft.layers) {
                auto &properties = layerProperties(layer);
                if (!previous.contains(QString::fromStdString(properties.id)) && !properties.artboardId) properties.artboardId = m_activeArtboard;
            }
            return true;
        };
        auto candidate = *document();
        if (!scopedEdit(candidate)) return false;
        const auto checked = validate(candidate);
        if (!checked.ok()) return fail(QString::fromStdString(checked.issues.front().message));
        recordDocumentChange(candidate);
        QScopedValueRollback<bool> guard(m_synchronizingDocument, true);
        if (!synchronizeDocument(candidate)) return false;
        m_canvas = std::move(candidate);
        clearError();
        return refresh();
    } catch (const std::exception &exception) { return fail(QString::fromUtf8(exception.what())); }
}
bool EditorCanvas::addPaintLayer() {
    if (!documentReady()) return fail(tr("Open or create a canvas first."));
    if (!infiniteCanvas() && static_cast<std::uint64_t>(editingWidth()) * editingHeight() > MediaLimits{}.maxPixelsPerFrame)
        return fail(tr("This canvas exceeds the bitmap layer allocation limit."));
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto asset = "paint.asset." + id, layer = "paint.layer." + id;
    if (!commit([&](Document &draft) {
        if (draft.canvasMode == CanvasMode::Infinite) draft.assets.emplace_back(ChunkedRasterAsset{asset, {}});
        else draft.assets.emplace_back(RasterAsset{asset, makeRasterLayer(editingWidth(), editingHeight())});
        draft.layers.emplace_back(StaticBitmapLayer{{layer, "Paint"}, StaticSource{asset}});
        return true;
    })) return false;
    return selectLayer(QString::fromStdString(layer));
}
bool EditorCanvas::placeImage(const QUrl &source, const QString &placement) {
    auto path = localPath(source);
    if (path.isEmpty() || !documentReady()) return fail(tr("Choose a local image and open a canvas first."));
    if (placement != "Fit" && placement != "Fill" && placement != "1:1") return fail(tr("Choose a supported image placement."));
    QString wrapError;
    const auto wrapper = dreamscapes::persistImageCanvas(path, &wrapError);
    if (wrapper.isEmpty()) return fail(wrapError);
    BitmapImportOptions options;
    options.assetId = "image.asset." + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    options.extendedCodecs = false;
    BitmapImportResult imported;
    if (QFileInfo(path).suffix().compare("iisc", Qt::CaseInsensitive) == 0) {
        auto native = dreamscapes::readImageCanvas(wrapper);
        if (!native.ok()) return fail(QString::fromStdString(native.result.message));
        if (native.document.canvasMode != CanvasMode::Finite
            || std::uint64_t(native.document.extent.width) * native.document.extent.height > MediaLimits{}.maxPixelsPerFrame)
            return fail(tr("Place a finite canvas of at most 64 megapixels."));
        auto rendered = renderFrame(native.document, 0);
        if (!rendered.ok()) return fail(QString::fromStdString(rendered.message));
        imported.asset = {options.assetId, std::move(rendered.pixels)};
    } else imported = importBitmap(path.toStdString(), options);
    if (!imported.ok()) return fail(QString::fromStdString(imported.result.message));
    const auto id = "image.layer." + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const double sx = double(editingWidth()) / imported.asset.pixels.width, sy = double(editingHeight()) / imported.asset.pixels.height;
    const double scale = placement == "1:1" ? 1 : placement == "Fill" ? std::max(sx, sy) : std::min(sx, sy);
    if (!commit([&](Document &draft) {
        LayerProperties properties{id, QFileInfo(path).fileName().toStdString()};
        properties.transform.m11 = properties.transform.m22 = scale;
        properties.transform.translationX = (editingWidth() - imported.asset.pixels.width * scale) / 2;
        properties.transform.translationY = (editingHeight() - imported.asset.pixels.height * scale) / 2;
        draft.layers.emplace_back(StaticBitmapLayer{properties, StaticSource{imported.asset.id}});
        draft.assets.emplace_back(std::move(imported.asset));
        return true;
    })) return false;
    return selectLayer(QString::fromStdString(id));
}
bool EditorCanvas::setLayerVisible(const QString &id, bool visible) {
    clearError();
    return editDocument([&](DocumentEditor &editor) { return editor.setLayerVisible(id.toStdString(), visible); }).ok();
}
bool EditorCanvas::setLayerOpacity(const QString &id, double opacity) {
    clearError();
    return editDocument([&](DocumentEditor &editor) { return editor.setLayerOpacity(id.toStdString(), opacity); }).ok();
}
bool EditorCanvas::setLayerName(const QString &id, const QString &name) {
    clearError();
    return editDocument([&](DocumentEditor &editor) { return editor.setLayerName(id.toStdString(), name.toStdString()); }).ok();
}
bool EditorCanvas::setLayerBlend(const QString &id, const QString &blend) {
    RasterBlendMode mode;
    if (blend == "Normal" || blend == "Pass through") mode = RasterBlendMode::SourceOver;
    else if (blend == "Multiply") mode = RasterBlendMode::Multiply;
    else if (blend == "Screen") mode = RasterBlendMode::Screen;
    else if (blend == "Overlay") mode = RasterBlendMode::Overlay;
    else return fail(tr("This blend mode is not supported by the canvas renderer."));
    clearError();
    return editDocument([&](DocumentEditor &editor) { return editor.setLayerBlendMode(id.toStdString(), mode); }).ok();
}
bool EditorCanvas::setLayerTransform(const QString &id, const QVariantMap &values) {
    if (m_positionLocks.contains(id)) return fail(tr("Unlock the layer position before changing its transform."));
    if (!documentReady()) return fail(tr("Open a canvas first."));
    const auto *layer = findLayer(*document(), id.toStdString());
    if (!layer) return fail(tr("Select an existing layer."));
    auto transform = layerProperties(*layer).transform;
    transform.translationX = values.value("x", transform.translationX).toDouble();
    transform.translationY = values.value("y", transform.translationY).toDouble();
    transform.m11 = values.value("scaleX", transform.m11).toDouble();
    transform.m22 = values.value("scaleY", transform.m22).toDouble();
    clearError();
    return editDocument([&](DocumentEditor &editor) { return editor.setLayerTransform(id.toStdString(), transform); }).ok();
}
bool EditorCanvas::removeLayer(const QString &id) {
    clearError();
    if (!editDocument([&](DocumentEditor &editor) { return editor.removeLayer(id.toStdString()); }).ok()) return false;
    if (m_selectedDocumentLayer == id) clearSelection();
    return true;
}
