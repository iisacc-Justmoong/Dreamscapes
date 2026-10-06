#include "EditorCanvas.h"
#include "EditorMedia.h"
#include "EditorToolUtils.h"
#include <iiSharedCanvas/Bitmap/BitmapCodec.h>
#include <iiSharedCanvas/Validation/Validation.h>
#include <iiFileProvider.h>
#include <QFileInfo>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>

using namespace iiSharedCanvas;
namespace {
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

EditorCanvas::EditorCanvas(QQuickItem *parent) : CanvasItem(parent), m_media(std::make_unique<EditorMedia>(this)) {
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
    connect(m_media.get(), &EditorMedia::changed, this, &EditorCanvas::toolStateChanged);
    connect(this, &CanvasItem::selectionChanged, this, [this] { invalidateToolPreview(); emit toolStateChanged(); });
    connect(this, &CanvasItem::revisionChanged, this, &EditorCanvas::toolStateChanged);
    connect(this, &CanvasItem::revisionChanged, this, &EditorCanvas::recordHistory);
    connect(this, &CanvasItem::documentChanged, this, &EditorCanvas::documentInfoChanged);
    connect(this, &CanvasItem::documentChanged, this, [this] { m_clippingKey.clear(); m_clippingImage = QUrl{}; });
    connect(this, &CanvasItem::revisionChanged, this, &EditorCanvas::documentInfoChanged);
    connect(this, &CanvasItem::selectionChanged, this, &EditorCanvas::documentInfoChanged);
    connect(this, &CanvasItem::lastErrorChanged, this, &EditorCanvas::errorChanged);
}
EditorCanvas::~EditorCanvas() { unbind(); }
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
        result.append(layerDescription(*it, selectedLayerId()));
    return result;
}
QVariantMap EditorCanvas::selectedLayer() const {
    for (const auto &entry : layers()) if (entry.toMap()["selected"].toBool()) {
        auto result = entry.toMap(); const auto id = result.value("id").toString();
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
    if (!documentReady()) return fail(tr("Open a canvas first."));
    const auto *layer = findLayer(*document(), id.toStdString());
    if (!layer) return fail(tr("Select an existing layer."));
    const auto *asset = resolveAssetAt(*document(), *layer, frame());
    if (std::holds_alternative<StaticBitmapLayer>(*layer) && asset && contentKind(*asset) == ContentKind::Raster) {
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
        cancelStroke();
        CanvasItem::clearSelection();
        m_selectedDocumentLayer = id;
        emit selectionChanged();
    }
    clearError();
    return true;
}
void EditorCanvas::clearSelection() {
    const bool hadDocumentSelection = !m_selectedDocumentLayer.isEmpty();
    m_selectedDocumentLayer.clear();
    CanvasItem::clearSelection();
    if (hadDocumentSelection) emit selectionChanged();
}
bool EditorCanvas::adopt(Document candidate, const QVariantMap &specification) {
    const auto validation = validate(candidate);
    if (!validation.ok()) return fail(QString::fromStdString(validation.issues.front().message));
    cancelStroke();
    m_historySuspended = true;
    m_selectedDocumentLayer.clear();
    clearAreaSelection(); m_elementBounds.clear(); m_elementSettings.clear(); m_textOrigins.clear(); m_textSettings.clear(); m_audioSource = {}; m_audioTrack.clear(); m_media->stop();
    unbind();
    m_workingFile.reset();
    m_canvas = std::move(candidate);
    if (!bind(m_canvas)) { m_historySuspended = false; return fail(lastError()); }
    m_specification = specification;
    if (!m_canvas.layers.empty()) selectLayer(QString::fromStdString(layerProperties(m_canvas.layers.back()).id));
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
    try {
        if (QFileInfo(path).suffix().compare("iisc", Qt::CaseInsensitive) == 0) {
            const auto prefix = iiFileProvider::File::readPrefix(path, 16);
            if (prefix.startsWith("SQLite format 3")) {
                auto file = std::make_unique<DocumentFile>();
                const auto opened = file->open(path.toStdString());
                if (!opened.ok()) return fail(QString::fromStdString(opened.message));
                if (asCopy) {
                    const auto extent = file->document()->extent;
                    return adopt(*file->document(), {{"name", QFileInfo(path).completeBaseName()}, {"pixelWidth", extent.width}, {"pixelHeight", extent.height}});
                }
                cancelStroke();
                m_historySuspended = true;
                m_selectedDocumentLayer.clear();
                if (!bind(*file)) { m_historySuspended = false; return fail(lastError()); }
                clearAreaSelection(); m_elementBounds.clear(); m_elementSettings.clear(); m_textOrigins.clear(); m_textSettings.clear(); m_audioSource = {}; m_audioTrack.clear(); m_media->stop();
                m_workingFile = std::move(file);
                m_specification = {{"name", QFileInfo(path).fileName()}, {"pixelWidth", canvasWidth()}, {"pixelHeight", canvasHeight()}};
                if (!document()->layers.empty()) selectLayer(QString::fromStdString(layerProperties(document()->layers.back()).id));
                resetHistory();
                clearError();
                emit specificationChanged();
                emit documentInfoChanged();
                fitToView();
                return true;
            }
            const auto bytes = iiFileProvider::File::read(path, SerializationLimits{}.maximumContainerBytes);
            auto decoded = decodeIisc({reinterpret_cast<const std::uint8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size())});
            if (!decoded.ok()) return fail(QString::fromStdString(decoded.error.message));
            const auto extent = decoded.document.extent;
            return adopt(std::move(decoded.document), {{"name", QFileInfo(path).fileName()}, {"pixelWidth", extent.width}, {"pixelHeight", extent.height}});
        }
        return openImages({source});
    } catch (const std::exception &exception) { return fail(QString::fromUtf8(exception.what())); }
}
bool EditorCanvas::openImages(const QVariantList &sources) {
    if (sources.isEmpty()) return fail(tr("Select at least one image."));
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
bool EditorCanvas::saveDocumentAs(const QUrl &destination) {
    const auto path = localPath(destination);
    if (!documentReady() || path.isEmpty() || QFileInfo(path).suffix().compare("iisc", Qt::CaseInsensitive) != 0)
        return fail(tr("Save the canvas to a local .iisc file."));
    if (liveStrokeActive()) return fail(tr("Finish the current stroke before saving."));
    if (!filePath().isEmpty() && QFileInfo(path).absoluteFilePath() == QFileInfo(filePath()).absoluteFilePath()) {
        clearError();
        return true; // Working-file edits have already committed synchronously.
    }
    auto file = std::make_unique<DocumentFile>();
    const auto created = file->create(path.toStdString(), *document());
    if (!created.ok()) return fail(QString::fromStdString(created.message));
    const auto selection = selectedLayerId();
    const auto scale = zoom(), x = panX(), y = panY();
    const auto currentFrame = frame();
    m_historySuspended = true;
    if (!bind(*file)) { m_historySuspended = false; return fail(lastError()); }
    m_historySuspended = false;
    m_workingFile = std::move(file);
    restoreView(scale, x, y, currentFrame, selection);
    clearError();
    emit documentInfoChanged();
    return true;
}
bool EditorCanvas::saveDocument() {
    return saveDocumentAs(QUrl::fromLocalFile(filePath()));
}
bool EditorCanvas::commit(const std::function<bool(Document &)> &edit) {
    if (!documentReady()) return fail(tr("Open or create a canvas first."));
    if (liveStrokeActive()) return fail(tr("Finish the current stroke before editing the document."));
    try {
        if (m_workingFile) {
            const auto result = m_workingFile->edit(edit);
            if (!result.ok()) return fail(QString::fromStdString(result.message));
        } else {
            auto candidate = *document();
            if (!edit(candidate)) return false;
            const auto checked = validate(candidate);
            if (!checked.ok()) return fail(QString::fromStdString(checked.issues.front().message));
            recordDocumentChange(candidate);
            m_canvas = std::move(candidate);
        }
        clearError();
        return refresh();
    } catch (const std::exception &exception) { return fail(QString::fromUtf8(exception.what())); }
}
bool EditorCanvas::addPaintLayer() {
    if (!documentReady()) return fail(tr("Open or create a canvas first."));
    if (!infiniteCanvas() && static_cast<std::uint64_t>(canvasWidth()) * canvasHeight() > MediaLimits{}.maxPixelsPerFrame)
        return fail(tr("This canvas exceeds the bitmap layer allocation limit."));
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto asset = "paint.asset." + id, layer = "paint.layer." + id;
    if (!commit([&](Document &draft) {
        if (draft.canvasMode == CanvasMode::Infinite) draft.assets.emplace_back(ChunkedRasterAsset{asset, {}});
        else draft.assets.emplace_back(RasterAsset{asset, makeRasterLayer(draft.extent.width, draft.extent.height)});
        draft.layers.emplace_back(StaticBitmapLayer{{layer, "Paint"}, StaticSource{asset}});
        return true;
    })) return false;
    return selectLayer(QString::fromStdString(layer));
}
bool EditorCanvas::placeImage(const QUrl &source, const QString &placement) {
    const auto path = localPath(source);
    if (path.isEmpty() || !documentReady()) return fail(tr("Choose a local image and open a canvas first."));
    if (placement != "Fit" && placement != "Fill" && placement != "1:1") return fail(tr("Choose a supported image placement."));
    BitmapImportOptions options;
    options.assetId = "image.asset." + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    options.extendedCodecs = false;
    auto imported = importBitmap(path.toStdString(), options);
    if (!imported.ok()) return fail(QString::fromStdString(imported.result.message));
    const auto id = "image.layer." + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const double sx = double(canvasWidth()) / imported.asset.pixels.width, sy = double(canvasHeight()) / imported.asset.pixels.height;
    const double scale = placement == "1:1" ? 1 : placement == "Fill" ? std::max(sx, sy) : std::min(sx, sy);
    if (!commit([&](Document &draft) {
        LayerProperties properties{id, QFileInfo(path).fileName().toStdString()};
        properties.transform.m11 = properties.transform.m22 = scale;
        properties.transform.translationX = (canvasWidth() - imported.asset.pixels.width * scale) / 2;
        properties.transform.translationY = (canvasHeight() - imported.asset.pixels.height * scale) / 2;
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
