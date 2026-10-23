#include "EditorRasterSource.h"
#include <iiSharedCanvas.h>
#include <algorithm>
#include <cmath>
namespace editorRaster {
std::expected<Geometry, std::string> describe(const iiSharedCanvas::Document &document,
    const std::string &id, iiSharedCanvas::FrameIndex frame) {
    using namespace iiSharedCanvas;
    if (frame >= document.timeline.frameCount) return std::unexpected("The captured raster frame is unavailable.");
    const auto *layer = findLayer(document, id);
    const auto *asset = layer ? resolveAssetAt(document, *layer, frame) : nullptr;
    if (!asset) return std::unexpected("The selected raster source is unavailable.");
    Geometry geometry; geometry.transform = sampleLayerAt(document, *layer, frame).transform;
    if (const auto *raster = std::get_if<RasterAsset>(asset)) geometry.extent = {raster->pixels.width, raster->pixels.height};
    else if (std::holds_alternative<ChunkedRasterAsset>(*asset)) {
        geometry.extent = document.extent;
        if (geometry.extent.width <= 0 || geometry.extent.height <= 0
            || std::uint64_t(geometry.extent.width) * geometry.extent.height > MaximumContiguousPixels)
            return std::unexpected("The selected chunked raster exceeds the contiguous processing limit.");
        // read() flattens the allocated window, whose pixel zero is this world
        // origin. Apply it before the sampled layer transform, not afterwards.
        const auto origin = canvasOrigin(document); auto &t = geometry.transform;
        t.translationX += t.m11 * origin.x + t.m21 * origin.y;
        t.translationY += t.m12 * origin.x + t.m22 * origin.y;
    } else return std::unexpected("Select a bitmap source for image processing.");
    const auto &t = geometry.transform;
    if (geometry.extent.width <= 0 || geometry.extent.height <= 0
        || !std::isfinite(t.m11) || !std::isfinite(t.m12) || !std::isfinite(t.m21) || !std::isfinite(t.m22)
        || !std::isfinite(t.translationX) || !std::isfinite(t.translationY))
        return std::unexpected("The raw raster geometry is invalid.");
    return geometry;
}
Source read(const EditorFrameSnapshot &snapshot, const std::string &id, std::stop_token stop) {
    using namespace iiSharedCanvas;
    if (stop.stop_requested()) return std::unexpected("The raster source request was cancelled.");
    if (!snapshot.document || snapshot.frame >= snapshot.document->timeline.frameCount)
        return std::unexpected("The captured raster document or frame is unavailable.");
    const auto &document = *snapshot.document;
    const auto *layer = findLayer(document, id);
    const auto *asset = layer ? resolveAssetAt(document, *layer, snapshot.frame) : nullptr;
    if (!asset) return std::unexpected("The selected raster source is unavailable.");
    if (const auto *raster = std::get_if<RasterAsset>(asset))
        return std::shared_ptr<const RasterLayer>(snapshot.document, &raster->pixels);
    const auto *chunks = std::get_if<ChunkedRasterAsset>(asset);
    if (!chunks) return std::unexpected("Select a bitmap source for image processing.");
    const auto width = document.extent.width, height = document.extent.height;
    if (width <= 0 || height <= 0 || std::uint64_t(width) * height > MaximumContiguousPixels)
        return std::unexpected("The selected chunked raster exceeds the contiguous processing limit.");
    auto flat = std::make_shared<RasterLayer>(makeRasterLayer(width, height, 0));
    const auto origin = canvasOrigin(document); const auto size = document.infiniteCanvas.chunkSize;
    for (const auto &chunk : chunks->chunks) {
        if (stop.stop_requested()) return std::unexpected("The raster source request was cancelled.");
        const auto x = std::int64_t(chunk.column) * size, y = std::int64_t(chunk.row) * size;
        const auto left = std::max<std::int64_t>(0, origin.x - x), top = std::max<std::int64_t>(0, origin.y - y);
        const auto right = std::min<std::int64_t>(size, std::int64_t(origin.x) + width - x);
        const auto bottom = std::min<std::int64_t>(size, std::int64_t(origin.y) + height - y);
        for (auto row = top; row < bottom; ++row) {
            if (stop.stop_requested()) return std::unexpected("The raster source request was cancelled.");
            for (auto col = left; col < right; ++col)
                flat->pixels[std::size_t(y + row - origin.y) * width + x + col - origin.x] = rasterLayerPixelAt(chunk.pixels, {int(col), int(row)});
        }
    }
    return std::shared_ptr<const RasterLayer>(std::move(flat));
}
}
