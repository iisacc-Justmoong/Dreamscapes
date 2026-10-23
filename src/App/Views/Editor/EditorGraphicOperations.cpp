#include "EditorCanvas.h"
#include "EditorGraphicAdjust.h"
#include "EditorGraphicHelper.h"
#include <QPointer>
#include <QJSValue>
#include <QBitmap>

using namespace iiSharedCanvas;
namespace {
bool detachParameter(QVariant &value, QString &error, int depth = 0) {
    if (depth > 64) { error = "The image parameters are nested too deeply."; return false; }
    if (value.metaType() == QMetaType::fromType<QJSValue>()) {
        const auto js = value.value<QJSValue>();
        if (js.isQObject() || js.isCallable()) { error = "Image parameters must contain values, not GUI objects or callbacks."; return false; }
        value = js.toVariant();
    }
    const auto objectFlags = QMetaType::PointerToQObject | QMetaType::SharedPointerToQObject
        | QMetaType::WeakPointerToQObject | QMetaType::TrackingPointerToQObject;
    if (value.metaType().flags() & objectFlags) {
        error = "Image parameters must contain values, not GUI objects."; return false;
    }
    if (value.metaType() == QMetaType::fromType<QPixmap>()) value = value.value<QPixmap>().toImage();
    else if (value.metaType() == QMetaType::fromType<QBitmap>()) value = value.value<QBitmap>().toImage();
    if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
        auto map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) if (!detachParameter(it.value(), error, depth + 1)) return false;
        value = map;
    } else if (value.metaType() == QMetaType::fromType<QVariantList>()) {
        auto list = value.toList();
        for (auto &item : list) if (!detachParameter(item, error, depth + 1)) return false;
        value = list;
    }
    return true;
}
}

QVariantMap EditorCanvas::graphicOperationState(const QString &operation) const {
    const bool supported = EditorGraphicAdjust::supports(operation);
    const auto *layer = documentReady() ? findLayer(*document(), selectedLayerId().toStdString()) : nullptr;
    const auto *board = layer && layerProperties(*layer).artboardId ? findArtboard(*document(), *layerProperties(*layer).artboardId) : nullptr;
    const bool visible = layer && belongsToActiveArtboard(*layer) && sampleLayerAt(*document(), *layer, frame()).visible && (!board || board->visible);
    const auto *asset = layer && rasterLayerSelected() ? resolveAssetAt(*document(), *layer, frame()) : nullptr;
    const bool chunked = asset && std::holds_alternative<ChunkedRasterAsset>(*asset);
    const bool pureHelper = EditorGraphicAdjust::isHelper(operation) && !EditorGraphicAdjust::helperNeedsImage(operation);
    const bool imagePixels = asset && (std::holds_alternative<RasterAsset>(*asset) || (chunked
        && std::uint64_t(document()->extent.width) * document()->extent.height <= EditorGraphicHelper::MaximumChunkedPixels));
    const auto reasonFor = [&](bool pixels) {
        if (!supported) return tr("This operation has no image processor.");
        if (!documentReady()) return tr("Open a canvas first.");
        if (!pixels || !visible) return tr("Select a visible bitmap layer within the image processing limit.");
        if (m_pixelLocks.contains(selectedLayerId())) return tr("The selected layer's pixels are locked.");
        return QString{};
    };
    const auto reason = reasonFor(imagePixels || (chunked && pureHelper));
    const auto imageReason = reasonFor(imagePixels);
    return {{"supported", supported}, {"enabled", reason.isEmpty()}, {"reason", reason},
        {"imageEnabled", imageReason.isEmpty()}, {"imageReason", imageReason}};
}

std::optional<graphicImage::Input> EditorCanvas::graphicImageInput(const QString &tool, const QString &operation,
    const QVariantMap &parameters, bool preview, QString &error) {
    const auto capability = graphicOperationState(operation);
    if (!capability.value("imageEnabled").toBool()) { error = capability.value("imageReason").toString(); return {}; }
    if (liveStrokeActive()) { error = tr("Finish the current stroke before processing an image."); return {}; }
    const auto canonical = EditorGraphicAdjust::canonicalOperation(operation);
    if (!preview && graphicImage::maskDiagnostic(canonical)) { error = tr("Mask View is a diagnostic preview. It does not replace layer pixels."); return {}; }
    const bool implicitAdjustment = canonical == "applyLocalAdjustments" && !EditorGraphicAdjust::usesAdjustmentCollection(parameters);
    if ((implicitAdjustment || canonical == "invertMask" || canonical == "refineMask"
        || canonical == "combineMasks" || graphicImage::maskDiagnostic(canonical)) && m_selection.alpha.empty()) {
        error = tr("Create a mask before using this operation."); return {};
    }
    const auto extent = editingExtent();
    if (!preview && graphicImage::maskOperation(canonical)
        && std::uint64_t(extent.width) * extent.height > MediaLimits{}.maxPixelsPerFrame) {
        error = tr("The canvas selection exceeds the mask image processing limit."); return {};
    }
    graphicImage::Input input;
    QVariant detached = parameters; if (!detachParameter(detached, error)) return {};
    input.tool = tool; input.operation = canonical; input.parameters = detached.toMap(); input.preview = preview;
    input.layer = selectedLayerId().toStdString(); input.asset = selectedRasterAssetId();
    input.documentSequence = m_graphicDocumentSequence; input.revision = revision(); input.selectionSequence = m_graphicSelectionSequence;
    input.observedTool = m_tool; input.extent = editingExtent(); input.origin = editingOrigin();
    if (graphicImage::usesSelection(canonical, preview)) input.selection = m_selection;
    if (graphicImage::usesSourcePixels(canonical, preview)) {
        input.snapshot = editingFrameSnapshot();
        if (toolPreviewMatches(tool) && m_previewGraphicOperation == input.operation) input.original = m_previewPixels;
    } else {
        const auto geometry = editorRaster::describe(*document(), input.layer, frame());
        if (!geometry) { error = QString::fromStdString(geometry.error()); return {}; }
        input.sourceGeometry = *geometry;
        input.snapshot.frame = frame(); input.snapshot.artboardId = editingArtboardId();
    }
    if (m_graphicMaskLayer == selectedLayerId() && m_graphicMaskDocumentSequence == m_graphicDocumentSequence) input.operand = m_graphicMaskOperand;
    return input;
}
bool EditorCanvas::graphicImageContextMatches(const graphicImage::Input &input, bool afterApply) const {
    return documentReady() && !liveStrokeActive() && m_graphicDocumentSequence == input.documentSequence
        && revision() == input.revision + (afterApply ? 1 : 0) && frame() == input.snapshot.frame
        && selectedLayerId().toStdString() == input.layer && selectedRasterAssetId() == input.asset
        && editingArtboardId() == input.snapshot.artboardId && m_tool == input.observedTool
        && m_graphicSelectionSequence == input.selectionSequence && !m_pixelLocks.contains(selectedLayerId());
}
bool EditorCanvas::applyGraphicImageResult(const graphicImage::Input &input, graphicImage::Result result, QString &error) {
    if (!graphicImageContextMatches(input)) { error = tr("The image target changed while processing. Run the command again."); return false; }
    if (!result.ok()) { error = result.error; return false; }
    const QPointer<EditorCanvas> owner(this);
    if (result.mask) {
        error = graphicImage::maskOutputShapeError(input, result); if (!error.isEmpty()) return false;
        if (result.replaceOperand) {
            m_graphicMaskOperand = std::move(result.operand); m_graphicMaskLayer = selectedLayerId();
            m_graphicMaskDocumentSequence = input.documentSequence;
        }
        m_selection = std::move(result.selection); m_selectedPixelCount = result.selectedPixels;
        m_selectionOverlay = result.selectionOverlay;
        updateSelectionOutline(result.selectionBits); if (!owner) return true;
        emit selectionOverlayChanged(); if (!owner) return true;
    } else {
        const bool replaced = replaceSelectedPixels(*result.pixels); if (!owner) return true;
        if (!replaced) { error = lastError(); return false; }
        // A direct observer can replace the document or perform another edit.
        // Publish the original only into the exact command context we changed.
        if (!graphicImageContextMatches(input, true)) return true;
        m_previewTool = input.tool; m_previewGraphicOperation = input.operation;
        m_previewLayer = QString::fromStdString(input.layer); m_previewPixels = std::move(result.original);
        m_previewAsset = input.asset; m_previewDocumentSequence = input.documentSequence; m_previewRevision = revision();
        m_touchedFields.clear();
    }
    clearError(); if (!owner) return true;
    emit toolStateChanged(); return true;
}
QVariantMap EditorCanvas::previewGraphicOperation(const QString &tool, const QString &operation, const QVariantMap &parameters) {
    QString error; auto input = graphicImageInput(tool, operation, parameters, true, error);
    if (!input) { fail(error); return {}; }
    auto result = graphicImage::calculate(*input);
    if (!result.ok()) { fail(result.error); return {}; }
    auto preview = std::move(result.preview); clearError(); return preview;
}
bool EditorCanvas::applyGraphicOperation(const QString &tool, const QString &operation, const QVariantMap &parameters) {
    QString error; auto input = graphicImageInput(tool, operation, parameters, false, error);
    if (!input) return fail(error);
    auto result = graphicImage::calculate(*input);
    return applyGraphicImageResult(*input, std::move(result), error) || fail(error);
}
