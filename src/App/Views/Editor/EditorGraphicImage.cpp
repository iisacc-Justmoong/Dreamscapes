#include "EditorGraphicImage.h"
#include "EditorGraphicAdjust.h"
#include "EditorRasterSource.h"
#include "EditorToolUtils.h"
#include <QPainter>
#include <QTransform>
#include <new>
#include <stdexcept>

using namespace iiSharedCanvas;
using namespace editorTools;
namespace graphicImage {
QString maskOutputShapeError(const Input &input, const Result &result) {
    const auto width = input.extent.width, height = input.extent.height;
    if (!result.mask || input.preview || result.pixels || width <= 0 || height <= 0
        || std::uint64_t(width) * height > MediaLimits{}.maxPixelsPerFrame
        || result.selection.extent != input.extent
        || result.selection.alpha.size() != std::size_t(width) * height
        || result.selectedPixels > result.selection.alpha.size())
        return "The generated mask dimensions or selection count are invalid.";
    const auto &bits = result.selectionBits;
    if (bits.isNull() || bits.size() != QSize(width, height) || bits.format() != QImage::Format_MonoLSB
        || bits.colorCount() < 2 || bits.color(0) != qRgb(255, 255, 255) || bits.color(1) != qRgb(0, 0, 0))
        return "The generated mask outline is invalid.";
    if (result.selectionOverlay.isEmpty()) return "The generated mask overlay could not be encoded.";
    if (result.replaceOperand && result.operand) {
        CanvasExtent expected;
        if (input.sourceGeometry) expected = input.sourceGeometry->extent;
        else if (result.original) expected = {result.original->width, result.original->height};
        else if (input.snapshot.document) {
            const auto geometry = editorRaster::describe(*input.snapshot.document, input.layer, input.snapshot.frame);
            if (!geometry) return QString::fromStdString(geometry.error());
            expected = geometry->extent;
        } else return "The mask operand source is unavailable.";
        if (result.operand->extent != expected || expected.width <= 0 || expected.height <= 0
            || result.operand->alpha.size() != std::size_t(expected.width) * expected.height)
            return "The generated mask operand is invalid.";
    }
    return {};
}
QString validateMaskOutput(const Input &input, const Result &result, std::stop_token stop) {
    if (const auto error = maskOutputShapeError(input, result); !error.isEmpty()) return error;
    if (stop.stop_requested()) return {};
    const auto url = result.selectionOverlay.toString(); const QString prefix = "data:image/png;base64,";
    if (!url.startsWith(prefix)) return "The generated mask overlay is not a PNG image.";
    const auto bytes = QByteArray::fromBase64(url.mid(prefix.size()).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (bytes.isEmpty()) return "The generated mask overlay could not be decoded.";
    auto overlay = QImage::fromData(bytes, "PNG");
    if (overlay.isNull() || overlay.size() != QSize(input.extent.width, input.extent.height))
        return "The generated mask overlay dimensions are invalid.";
    overlay = overlay.convertToFormat(QImage::Format_ARGB32);
    if (overlay.isNull()) return "The generated mask overlay could not be retained.";
    std::size_t selected = 0;
    for (int y = 0; y < input.extent.height; ++y) {
        if (stop.stop_requested()) return {};
        const auto *pixels = reinterpret_cast<const QRgb *>(overlay.constScanLine(y));
        const auto *bits = result.selectionBits.constScanLine(y);
        for (int x = 0; x < input.extent.width; ++x) {
            const auto alpha = result.selection.alpha[std::size_t(y) * input.extent.width + x];
            selected += alpha != 0;
            if (bool(bits[x / 8] & (1 << (x % 8))) != (alpha >= 128) || qAlpha(pixels[x]) != alpha / 3)
                return "The generated mask pixels, outline and overlay disagree.";
        }
    }
    return selected == result.selectedPixels ? QString{} : QString("The generated mask selection count is inconsistent.");
}
bool maskOperation(const QString &op) {
    return QStringList{"brushMask", "linearMask", "radialMask", "colorRangeMask", "luminanceRangeMask", "refineMask", "invertMask", "combineMasks"}.contains(op);
}
bool maskDiagnostic(const QString &op) {
    return QStringList{"maskColorOverlay", "maskColorOnBwOverlay", "maskImageOnBlack", "maskImageOnWhite", "maskWhiteOnBlack"}.contains(op);
}
bool usesSelection(const QString &op, bool preview) {
    return !preview || !maskOperation(op) || op == "invertMask" || op == "refineMask" || op == "combineMasks";
}
bool usesSourcePixels(const QString &op, bool preview) {
    return preview || !maskOperation(op) || EditorGraphicAdjust::maskNeedsPixels(op);
}
namespace {
std::optional<iiGraphicAdjust::Mask> nativeMask(const RasterMask &mask, std::stop_token stop) {
    iiGraphicAdjust::Mask result{std::size_t(mask.extent.width), std::size_t(mask.extent.height), {}};
    result.coverage.resize(mask.alpha.size());
    for (std::size_t i = 0; i < mask.alpha.size(); ++i) {
        if (i % 65536 == 0 && stop.stop_requested()) return {};
        // Preserve the public adapter's double-to-float rounding exactly.
        result.coverage[i] = float(mask.alpha[i] / 255.0);
    }
    return result;
}
}
Result calculate(const Input &input, std::stop_token stop) try {
    Result out;
    const auto stopped = [&] { out.cancelled = stop.stop_requested(); return out.cancelled; };
    const auto fail = [](QString error) { Result failed; failed.error = std::move(error); return failed; };
    if (stopped()) return out;
    const bool needsPixels = usesSourcePixels(input.operation, input.preview);
    SourceGeometry geometry;
    std::shared_ptr<const RasterLayer> source;
    if (input.sourceGeometry) {
        if (needsPixels) return fail("This image operation requires captured source pixels.");
        geometry = *input.sourceGeometry;
    } else {
        if (!input.snapshot.document || input.snapshot.frame >= input.snapshot.document->timeline.frameCount)
            return fail("The captured image document or frame is unavailable.");
        const auto &document = *input.snapshot.document;
        auto description = editorRaster::describe(document, input.layer, input.snapshot.frame);
        if (!description) return fail(QString::fromStdString(description.error()));
        geometry = *description;
        if (needsPixels) {
            source = input.original;
            if (!source) {
                auto captured = editorRaster::read(input.snapshot, input.layer, stop);
                if (!captured) { if (stopped()) return out; return fail(QString::fromStdString(captured.error())); }
                source = std::move(*captured);
            }
            geometry.extent = {source->width, source->height};
        } else if (input.original) geometry.extent = {input.original->width, input.original->height};
    }
    if (geometry.extent.width <= 0 || geometry.extent.height <= 0) return fail("The captured mask dimensions are invalid.");
    out.original = source;
    const auto width = geometry.extent.width, height = geometry.extent.height;
    const auto &t = geometry.transform;
    const QTransform transform(t.m11, t.m12, t.m21, t.m22, t.translationX - input.origin.x(), t.translationY - input.origin.y());
    RasterMask local;
    if (usesSelection(input.operation, input.preview) && !input.selection.alpha.empty()) {
        if (input.selection.extent.width <= 0 || input.selection.extent.height <= 0
            || input.selection.alpha.size() != std::size_t(input.selection.extent.width) * input.selection.extent.height)
            return fail("The captured image selection is invalid.");
        local = {{width, height}, std::vector<std::uint8_t>(std::size_t(width) * height, 0)};
        for (int y = 0; y < height; ++y) {
            if (stopped()) return out;
            for (int x = 0; x < width; ++x) {
                const auto p = transform.map(QPointF(x + .5, y + .5)); const int px = int(std::floor(p.x())), py = int(std::floor(p.y()));
                if (px >= 0 && py >= 0 && px < input.selection.extent.width && py < input.selection.extent.height)
                    local.alpha[std::size_t(y) * width + x] = input.selection.alpha[std::size_t(py) * input.selection.extent.width + px];
            }
        }
    }
    auto args = input.parameters; const auto &op = input.operation;
    const bool implicitAdjustment = op == "applyLocalAdjustments" && !EditorGraphicAdjust::usesAdjustmentCollection(args);
    EditorGraphicAdjust::MaskInputs masks;
    std::optional<iiGraphicAdjust::Mask> localInput, operandInput;
    if (implicitAdjustment || op == "invertMask" || op == "refineMask" || maskDiagnostic(op) || op == "combineMasks") {
        if (local.alpha.empty()) return fail("Create a mask before using this operation.");
        localInput = nativeMask(local, stop);
        if (stopped()) return out;
        if (op == "combineMasks") masks.base = localInput->view();
        else masks.mask = localInput->view();
    }
    if (op == "combineMasks") {
        if (!args.contains("operand")) {
            if (!input.operand) return fail("Create a second mask before combining masks.");
            operandInput = nativeMask(*input.operand, stop);
            if (stopped()) return out;
            masks.operand = operandInput->view();
        }
    }
    if (stopped()) return out;
    if (maskOperation(op)) {
        auto generated = EditorGraphicAdjust::maskNeedsPixels(op)
            ? EditorGraphicAdjust::makeMask(image(*source), op, args, masks)
            : EditorGraphicAdjust::makeMask(QSize(width, height), op, args, masks);
        if (!generated.ok()) return fail(generated.error);
        out.mask = true;
        QImage localImage(generated.width, generated.height, QImage::Format_ARGB32);
        if (localImage.isNull()) return fail("Could not allocate the generated mask image.");
        for (int y = 0; y < localImage.height(); ++y) {
            if (stopped()) return out;
            auto *line = reinterpret_cast<QRgb *>(localImage.scanLine(y));
            for (int x = 0; x < localImage.width(); ++x) {
                const auto level = qRound(std::clamp(generated.coverage[std::size_t(y) * localImage.width() + x], 0.f, 1.f) * 255);
                line[x] = input.preview ? qRgb(level, level, level) : qRgba(255, 255, 255, level);
            }
        }
        if (input.preview) {
            out.preview = {{"imageSource", dataImage(localImage)}, {"originalImageSource", dataImage(image(*source))}, {"mask", true}};
            if (out.preview["imageSource"].toUrl().isEmpty() || out.preview["originalImageSource"].toUrl().isEmpty())
                return fail("Could not encode the mask preview or original image.");
        } else {
            if (input.extent.width <= 0 || input.extent.height <= 0
                || std::uint64_t(input.extent.width) * input.extent.height > MediaLimits{}.maxPixelsPerFrame)
                return fail("The canvas selection exceeds the mask image processing limit.");
            QImage mapped(input.extent.width, input.extent.height, QImage::Format_ARGB32);
            if (mapped.isNull()) return fail("Could not allocate the canvas mask image.");
            mapped.fill(Qt::transparent);
            QPainter painter(&mapped);
            if (!painter.isActive()) return fail("Could not prepare the canvas mask image.");
            painter.setTransform(transform); painter.drawImage(QPointF{}, localImage); painter.end();
            out.selection = {input.extent, std::vector<std::uint8_t>(std::size_t(input.extent.width) * input.extent.height)};
            out.selectionBits = QImage(mapped.size(), QImage::Format_MonoLSB);
            if (out.selectionBits.isNull()) return fail("Could not allocate the mask outline.");
            out.selectionBits.fill(0);
            out.selectionBits.setColor(0, qRgb(255, 255, 255)); out.selectionBits.setColor(1, qRgb(0, 0, 0));
            QImage overlay(mapped.size(), QImage::Format_ARGB32);
            if (overlay.isNull()) return fail("Could not allocate the mask overlay.");
            overlay.fill(Qt::transparent);
            for (int y = 0; y < mapped.height(); ++y) {
                if (stopped()) return out;
                const auto *row = reinterpret_cast<const QRgb *>(mapped.constScanLine(y));
                auto *bits = out.selectionBits.scanLine(y); auto *colors = reinterpret_cast<QRgb *>(overlay.scanLine(y));
                for (int x = 0; x < mapped.width(); ++x) {
                    const auto alpha = std::uint8_t(qAlpha(row[x])); out.selection.alpha[std::size_t(y) * mapped.width() + x] = alpha;
                    if (alpha) { ++out.selectedPixels; colors[x] = (std::uint32_t(alpha / 3) << 24) | 0x008b7cff; }
                    if (alpha >= 128) bits[x / 8] |= 1 << (x % 8);
                }
            }
            out.selectionOverlay = dataImage(overlay);
            if (out.selectionOverlay.isEmpty()) return fail("Could not encode the mask overlay.");
            out.replaceOperand = op != "invertMask" && op != "refineMask" && op != "combineMasks";
            if (out.replaceOperand && !local.alpha.empty()) out.operand = std::make_shared<const RasterMask>(std::move(local));
        }
    } else {
        if (!input.preview && maskDiagnostic(op)) return fail("Mask View is a diagnostic preview. It does not replace layer pixels.");
        auto processed = EditorGraphicAdjust::process(image(*source), op, args, masks);
        if (!processed.ok()) return fail(processed.error);
        if (stopped()) return out;
        auto pixels = raster(processed.image);
        if (!local.alpha.empty() && !maskDiagnostic(op) && !implicitAdjustment) {
            auto blended = blendRaster(*source, pixels, local);
            if (!blended.ok()) return fail(QString::fromStdString(blended.error));
            pixels = std::move(blended.pixels);
        }
        out.pixels = std::make_shared<const RasterLayer>(std::move(pixels)); out.parameters = processed.parameters;
        if (input.preview) {
            out.preview = {{"imageSource", dataImage(image(*out.pixels))}, {"originalImageSource", dataImage(image(*source))}, {"parameters", out.parameters}, {"mask", false}};
            if (out.preview["imageSource"].toUrl().isEmpty() || out.preview["originalImageSource"].toUrl().isEmpty())
                return fail("Could not encode the image preview or original image.");
        }
    }
    (void)stopped(); return out;
} catch (const std::bad_alloc &) {
    Result failed; failed.error = "Image processing allocation failed."; return failed;
} catch (const std::length_error &) {
    Result failed; failed.error = "Image processing storage exceeds the supported size."; return failed;
}
}
