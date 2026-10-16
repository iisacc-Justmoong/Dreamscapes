#include "EditorCanvas.h"
#include "EditorGraphicAdjust.h"
#include "EditorToolUtils.h"
#include <QPainter>
#include <QTransform>

using namespace iiSharedCanvas;
using namespace editorTools;

QVariantMap EditorCanvas::graphicOperationState(const QString &operation) const {
    const bool supported = EditorGraphicAdjust::supports(operation);
    const auto *layer = documentReady() ? findLayer(*document(), selectedLayerId().toStdString()) : nullptr;
    const auto *board = layer && layerProperties(*layer).artboardId ? findArtboard(*document(), *layerProperties(*layer).artboardId) : nullptr;
    const bool visible = layer && belongsToActiveArtboard(*layer) && sampleLayerAt(*document(), *layer, frame()).visible && (!board || board->visible);
    QString reason;
    if (!supported) reason = tr("This operation has no image processor.");
    else if (!documentReady()) reason = tr("Open a canvas first.");
    else if (!selectedRasterPixels() || !visible) reason = tr("Select a visible bitmap layer.");
    else if (m_pixelLocks.contains(selectedLayerId())) reason = tr("The selected layer's pixels are locked.");
    return {{"supported", supported}, {"enabled", reason.isEmpty()}, {"reason", reason}};
}

namespace {
bool maskOperation(const QString &operation) {
    return QStringList{"brushMask", "linearMask", "radialMask", "colorRangeMask", "luminanceRangeMask", "refineMask", "invertMask", "combineMasks"}.contains(operation);
}
bool maskDiagnostic(const QString &operation) {
    return QStringList{"maskColorOverlay", "maskColorOnBwOverlay", "maskImageOnBlack", "maskImageOnWhite", "maskWhiteOnBlack"}.contains(operation);
}
QVariantMap maskParameters(const RasterMask &mask) {
    QVariantList samples;
    samples.reserve(qsizetype(mask.alpha.size()));
    for (auto alpha : mask.alpha) samples.append(alpha / 255.0);
    return {{"width", mask.extent.width}, {"height", mask.extent.height}, {"coverage", samples}};
}
}

QVariantMap EditorCanvas::previewGraphicOperation(const QString &tool, const QString &operation, const QVariantMap &parameters) {
    const auto capability = graphicOperationState(operation);
    if (!capability.value("enabled").toBool()) { fail(capability.value("reason").toString()); return {}; }
    const auto canonical = EditorGraphicAdjust::canonicalOperation(operation);
    const bool session = m_previewTool == tool && m_previewGraphicOperation == canonical
        && m_previewLayer == selectedLayerId() && m_previewRevision == revision();
    const auto &source = session ? m_previewPixels : *selectedRasterPixels();
    auto arguments = parameters;
    if (canonical == "applyLocalAdjustments" || canonical == "invertMask" || canonical == "refineMask" || maskDiagnostic(canonical)) {
        if (m_selection.alpha.empty()) { fail(tr("Create a mask before using this operation.")); return {}; }
        arguments["mask"] = maskParameters(layerMask());
    }
    if (canonical == "combineMasks") {
        if (m_selection.alpha.empty()) { fail(tr("Create a mask before combining it.")); return {}; }
        arguments["base"] = maskParameters(layerMask());
        if (!arguments.contains("operand")) {
            if (m_graphicMaskOperand.isEmpty() || m_graphicMaskLayer != selectedLayerId() || m_graphicMaskDocumentSequence != m_graphicDocumentSequence) { fail(tr("Create a second mask before combining masks.")); return {}; }
            arguments["operand"] = m_graphicMaskOperand;
        }
    }
    if (maskOperation(canonical)) {
        auto generated = EditorGraphicAdjust::makeMask(image(source), canonical, arguments);
        if (!generated.ok()) { fail(generated.error); return {}; }
        QImage diagnostic(generated.width, generated.height, QImage::Format_ARGB32);
        for (int y = 0; y < diagnostic.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(diagnostic.scanLine(y));
            for (int x = 0; x < diagnostic.width(); ++x) {
                const auto level = qRound(std::clamp(generated.coverage[std::size_t(y) * diagnostic.width() + x], 0.0f, 1.0f) * 255);
                line[x] = qRgb(level, level, level);
            }
        }
        clearError();
        return {{"imageSource", dataImage(diagnostic)}, {"originalImageSource", dataImage(image(source))}, {"mask", true}};
    }
    auto processed = EditorGraphicAdjust::process(image(source), canonical, arguments);
    if (!processed.ok()) { fail(processed.error); return {}; }
    auto result = raster(processed.image);
    const auto mask = layerMask();
    if (!mask.alpha.empty() && !maskDiagnostic(canonical) && canonical != "applyLocalAdjustments") {
        auto blended = blendRaster(source, result, mask);
        if (!blended.ok()) { fail(QString::fromStdString(blended.error)); return {}; }
        result = std::move(blended.pixels);
    }
    clearError();
    return {{"imageSource", dataImage(image(result))}, {"originalImageSource", dataImage(image(source))}, {"parameters", processed.parameters}, {"mask", false}};
}

bool EditorCanvas::applyGraphicOperation(const QString &tool, const QString &operation, const QVariantMap &parameters) {
    const auto capability = graphicOperationState(operation);
    if (!capability.value("enabled").toBool()) return fail(capability.value("reason").toString());
    const auto canonical = EditorGraphicAdjust::canonicalOperation(operation);
    auto arguments = parameters;
    if (canonical == "applyLocalAdjustments" || canonical == "invertMask" || canonical == "refineMask") {
        if (m_selection.alpha.empty()) return fail(tr("Create a mask before using this operation."));
        arguments["mask"] = maskParameters(layerMask());
    }
    if (canonical == "combineMasks") {
        if (m_selection.alpha.empty()) return fail(tr("Create a mask before combining it."));
        arguments["base"] = maskParameters(layerMask());
        if (!arguments.contains("operand")) {
            if (m_graphicMaskOperand.isEmpty() || m_graphicMaskLayer != selectedLayerId() || m_graphicMaskDocumentSequence != m_graphicDocumentSequence) return fail(tr("Create a second mask before combining masks."));
            arguments["operand"] = m_graphicMaskOperand;
        }
    }
    if (maskOperation(canonical)) {
        const auto generated = EditorGraphicAdjust::makeMask(image(*selectedRasterPixels()), canonical, arguments);
        if (!generated.ok()) return fail(generated.error);
        RasterMask layer{{generated.width, generated.height}, {}};
        layer.alpha.reserve(generated.coverage.size());
        for (auto coverage : generated.coverage) layer.alpha.push_back(std::uint8_t(qRound(std::clamp(coverage, 0.0f, 1.0f) * 255)));
        // A SDK mask uses raster-local coordinates; editor selection uses the
        // active artboard. Map through the selected layer's real transform.
        const auto *selected = findLayer(*document(), selectedLayerId().toStdString());
        const auto &t = layerProperties(*selected).transform;
        QImage local(generated.width, generated.height, QImage::Format_ARGB32);
        for (int y = 0; y < local.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(local.scanLine(y));
            for (int x = 0; x < local.width(); ++x) line[x] = qRgba(255, 255, 255, layer.alpha[std::size_t(y) * local.width() + x]);
        }
        QImage mapped(editingWidth(), editingHeight(), QImage::Format_ARGB32); mapped.fill(Qt::transparent);
        QPainter painter(&mapped);
        const auto origin = editingOrigin();
        painter.setTransform(QTransform(t.m11, t.m12, t.m21, t.m22, t.translationX - origin.x(), t.translationY - origin.y()));
        painter.drawImage(QPointF{}, local); painter.end();
        if (canonical != "invertMask" && canonical != "refineMask" && canonical != "combineMasks") {
            m_graphicMaskOperand = m_selection.alpha.empty() ? QVariantMap{} : maskParameters(layerMask());
            m_graphicMaskLayer = selectedLayerId(); m_graphicMaskDocumentSequence = m_graphicDocumentSequence;
        }
        m_selection = {{editingWidth(), editingHeight()}, {}};
        m_selection.alpha.reserve(std::size_t(editingWidth()) * editingHeight());
        for (int y = 0; y < mapped.height(); ++y) for (int x = 0; x < mapped.width(); ++x) m_selection.alpha.push_back(std::uint8_t(qAlpha(mapped.pixel(x, y))));
        updateSelectionOverlay(); clearError(); emit toolStateChanged(); return true;
    }
    if (maskDiagnostic(canonical)) { fail(tr("Mask View is a diagnostic preview. It does not replace layer pixels.")); return false; }
    const bool session = m_previewTool == tool && m_previewGraphicOperation == canonical
        && m_previewLayer == selectedLayerId() && m_previewRevision == revision();
    const auto source = session ? m_previewPixels : *selectedRasterPixels();
    auto processed = EditorGraphicAdjust::process(image(source), canonical, arguments);
    if (!processed.ok()) return fail(processed.error);
    auto pixels = raster(processed.image);
    const auto mask = layerMask();
    if (!mask.alpha.empty() && canonical != "applyLocalAdjustments") {
        auto blended = blendRaster(source, pixels, mask);
        if (!blended.ok()) return fail(QString::fromStdString(blended.error));
        pixels = std::move(blended.pixels);
    }
    if (!replaceSelectedPixels(pixels)) return false;
    m_previewTool = tool; m_previewGraphicOperation = canonical;
    m_previewLayer = selectedLayerId(); m_previewPixels = source; m_previewRevision = revision();
    clearError(); emit toolStateChanged(); return true;
}
