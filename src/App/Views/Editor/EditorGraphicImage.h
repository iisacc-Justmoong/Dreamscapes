#pragma once
#include "EditorRasterSource.h"
#include <iiSharedCanvas.h>
#include <QImage>
#include <QPointF>
#include <QVariantMap>
#include <QUrl>
#include <memory>
#include <optional>
#include <stop_token>

// Detached GUI/image boundary. SDK image mathematics remain Qt-free.
namespace graphicImage {
using SourceGeometry = editorRaster::Geometry;
struct Input {
    EditorFrameSnapshot snapshot;
    // Geometry mask application captures these values instead of a document.
    std::optional<SourceGeometry> sourceGeometry;
    std::string layer;
    QString tool, operation;
    QVariantMap parameters;
    std::shared_ptr<const RasterLayer> original;
    iiSharedCanvas::RasterMask selection;
    std::shared_ptr<const iiSharedCanvas::RasterMask> operand;
    iiSharedCanvas::CanvasExtent extent;
    QPointF origin;
    QString observedTool;
    std::string asset;
    std::uint64_t documentSequence = 0, revision = 0, selectionSequence = 0;
    bool preview = false;
};
struct Result {
    std::shared_ptr<const RasterLayer> original, pixels;
    std::shared_ptr<const iiSharedCanvas::RasterMask> operand;
    iiSharedCanvas::RasterMask selection;
    QImage selectionBits;
    QUrl selectionOverlay;
    std::size_t selectedPixels = 0;
    QVariantMap preview, parameters;
    QString error;
    bool mask = false, replaceOperand = false, cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled && (pixels || mask); }
};
Result calculate(const Input &input, std::stop_token stop = {});
// Metadata checks run before owner-state mutation, without scanning pixels.
// Full content checks run on the worker, including count, outline and PNG coverage.
QString maskOutputShapeError(const Input &input, const Result &result);
QString validateMaskOutput(const Input &input, const Result &result, std::stop_token stop = {});
bool maskOperation(const QString &operation);
bool maskDiagnostic(const QString &operation);
bool usesSelection(const QString &operation, bool preview);
bool usesSourcePixels(const QString &operation, bool preview);
}
