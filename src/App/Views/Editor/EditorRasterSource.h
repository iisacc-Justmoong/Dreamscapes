#pragma once
#include "EditorFrameSnapshot.h"
#include <expected>
#include <cstdint>
#include <stop_token>
namespace editorRaster {
inline constexpr std::uint64_t MaximumContiguousPixels = 16U * 1024U * 1024U;
using Source = std::expected<std::shared_ptr<const RasterLayer>, std::string>;
struct Geometry {
    iiSharedCanvas::CanvasExtent extent;
    // Maps zero-based raw image pixels into document world coordinates.
    AffineTransform transform;
};
std::expected<Geometry, std::string> describe(const iiSharedCanvas::Document &document,
    const std::string &layer, iiSharedCanvas::FrameIndex frame);
// Raw selected source, with no composite, opacity, transform or mask applied.
Source read(const EditorFrameSnapshot &snapshot, const std::string &layer, std::stop_token stop = {});
}
