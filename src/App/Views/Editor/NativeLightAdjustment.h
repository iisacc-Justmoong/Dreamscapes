#pragma once
#include <Document/Document.h>
#include <iiGraphicAdjust>
#include <iiSDKCore>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dreamscapes::editor {
// A worker owns the candidate; the session remains the only commit authority.
struct PreparedLightAdjustment final {
    std::shared_ptr<const iiSharedCanvas::Document> source;
    std::shared_ptr<iiSharedCanvas::Document> candidate;
    std::uint64_t generation=0,revision=0;
    std::string layerId;
    iiSharedCanvas::FrameIndex frame=0;
};
// Analysis reads a snapshot and never changes the canvas or a shared asset.
[[nodiscard]] ii::core::Result<iiGraphicAdjust::DevelopLightOptions> analyzeLightAdjustment(
    std::shared_ptr<const iiSharedCanvas::Document>,const std::string &layerId,
    iiSharedCanvas::FrameIndex,std::function<bool()> cancelled={});
[[nodiscard]] ii::core::Result<PreparedLightAdjustment> prepareLightAdjustment(
    std::shared_ptr<const iiSharedCanvas::Document>,std::uint64_t generation,std::uint64_t revision,
    std::string layerId,iiSharedCanvas::FrameIndex,const iiGraphicAdjust::DevelopLightOptions &,
    std::function<bool()> cancelled={});
}
