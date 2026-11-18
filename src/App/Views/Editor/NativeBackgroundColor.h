#pragma once
#include <Document/Document.h>
#include <iiColorManager>
#include <iiSDKCore>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace dreamscapes::editor {
enum class BackgroundFillStyle {Solid,Linear,Radial};
enum class BackgroundImageFit {Cover,Contain,Stretch};
struct BackgroundStop final {double position=0;iiColorManager::Rgba color;};
struct BackgroundFillSpec final {
    BackgroundFillStyle style=BackgroundFillStyle::Solid;
    iiColorManager::Rgba color;
    double alpha=1,opacity=1,angleDegrees=90;
    bool perceptual=true;
    std::vector<BackgroundStop> stops;
};
// Built from an immutable document snapshot on a worker. Only the owner
// session can commit it, and only while the captured document is current.
struct PreparedBackgroundFill final {
    std::shared_ptr<const iiSharedCanvas::Document> source;
    std::shared_ptr<iiSharedCanvas::Document> candidate;
    std::uint64_t generation=0,revision=0;
    bool inserted=false;
};
[[nodiscard]] ii::core::Result<PreparedBackgroundFill> prepareBackgroundFill(
    std::shared_ptr<const iiSharedCanvas::Document>,std::uint64_t generation,std::uint64_t revision,
    const BackgroundFillSpec &,std::function<bool()> cancelled={});
[[nodiscard]] ii::core::Result<PreparedBackgroundFill> prepareBackgroundImage(
    std::shared_ptr<const iiSharedCanvas::Document>,std::uint64_t generation,std::uint64_t revision,
    const RasterLayer &,BackgroundImageFit,std::function<bool()> cancelled={});
}
