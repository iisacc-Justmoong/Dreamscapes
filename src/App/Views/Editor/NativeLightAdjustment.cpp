#include "NativeLightAdjustment.h"
#include <Document/DocumentEditor.h>
#include <Media/MediaIo.h>
#include <Validation/Validation.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace dreamscapes::editor {
using ii::core::ErrorCode;
using namespace iiSharedCanvas;
namespace {
constexpr std::uint64_t workingBytesPerPixel=128;
bool between(double value,double minimum,double maximum) {
    return std::isfinite(value) && value>=minimum && value<=maximum;
}
ii::core::Result<const RasterAsset *> selectedRaster(const std::shared_ptr<const Document> &source,
    const std::string &layerId,FrameIndex frame) {
    if (!source) return ii::core::fail(ErrorCode::closed,"Open a canvas first");
    if (frame>=source->timeline.frameCount) return ii::core::fail(ErrorCode::out_of_range,"The frame is outside the canvas");
    const auto *layer=findLayer(*source,layerId);
    const auto *bitmap=layer?std::get_if<StaticBitmapLayer>(layer):nullptr;
    if (!bitmap) return ii::core::fail(ErrorCode::unsupported,"Select a static bitmap layer");
    const auto *asset=findRasterAsset(*source,bitmap->content.assetId);
    if (!asset || asset->pixels.width<=0 || asset->pixels.height<=0)
        return ii::core::fail(ErrorCode::invalid_argument,"The selected bitmap is unavailable");
    const auto count=std::uint64_t(asset->pixels.width)*std::uint64_t(asset->pixels.height);
    const MediaLimits limits;
    if (count!=asset->pixels.pixels.size() || count>limits.maxPixelsPerFrame ||
        count>limits.maxDecodedBytes/workingBytesPerPixel)
        return ii::core::fail(ErrorCode::out_of_range,"The selected bitmap exceeds the light adjustment memory limit");
    return asset;
}
}
ii::core::Result<iiGraphicAdjust::DevelopLightOptions> analyzeLightAdjustment(std::shared_ptr<const Document> source,
    const std::string &layerId,FrameIndex frame,std::function<bool()> cancelled) {
    auto selected=selectedRaster(source,layerId,frame);
    if (!selected) return std::unexpected(selected.error());
    try {
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"Light analysis was cancelled");
        const auto *asset=*selected;
        auto input=iiGraphicAdjust::fromArgb32(asset->pixels.width,asset->pixels.height,asset->pixels.pixels);
        if (!input) return ii::core::fail(ErrorCode::failed,std::string(input.error().message));
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"Light analysis was cancelled");
        auto result=iiGraphicAdjust::autoDevelopLight(input->view());
        if (!result) return ii::core::fail(ErrorCode::failed,std::string(result.error().message));
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"Light analysis was cancelled");
        return *result;
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot analyze the selected bitmap",std::current_exception());}
}
ii::core::Result<PreparedLightAdjustment> prepareLightAdjustment(std::shared_ptr<const Document> source,
    std::uint64_t generation,std::uint64_t revision,std::string layerId,FrameIndex frame,
    const iiGraphicAdjust::DevelopLightOptions &options,std::function<bool()> cancelled) {
    if (!between(options.exposure,-5,5) || !between(options.contrast,-100,100) ||
        !between(options.highlights,-100,100) || !between(options.shadows,-100,100) ||
        !between(options.whites,-100,100) || !between(options.blacks,-100,100))
        return ii::core::fail(ErrorCode::invalid_argument,"Light values are outside their supported ranges");
    auto selected=selectedRaster(source,layerId,frame);
    if (!selected) return std::unexpected(selected.error());
    const auto *asset=*selected;
    try {
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"Light adjustment was cancelled");
        auto input=iiGraphicAdjust::fromArgb32(asset->pixels.width,asset->pixels.height,asset->pixels.pixels);
        if (!input) return ii::core::fail(ErrorCode::failed,std::string(input.error().message));
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"Light adjustment was cancelled");
        auto adjusted=iiGraphicAdjust::developLight(input->view(),options);
        if (!adjusted) return ii::core::fail(ErrorCode::failed,std::string(adjusted.error().message));
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"Light adjustment was cancelled");
        auto output=iiGraphicAdjust::toArgb32(adjusted->view());
        if (!output) return ii::core::fail(ErrorCode::failed,std::string(output.error().message));
        auto candidate=std::make_shared<Document>(*source);
        std::string newAsset;
        for (std::uint64_t i=1;i<std::numeric_limits<std::uint64_t>::max();++i) {
            auto id="dreamscapes.light."+std::to_string(i);
            if (!findAsset(*candidate,id) && !findAudioAsset(*candidate,id)) {newAsset=std::move(id);break;}
        }
        if (newAsset.empty()) return ii::core::fail(ErrorCode::out_of_range,"Light adjustment asset identity is exhausted");
        RasterLayer raster{asset->pixels.width,asset->pixels.height,std::move(*output)};
        DocumentEditor editor(*candidate);
        const auto inserted=editor.insertRasterAsset(newAsset,std::move(raster));
        if (!inserted.ok()) return ii::core::fail(ErrorCode::invalid_argument,inserted.message);
        auto *target=std::get_if<StaticBitmapLayer>(findLayer(*candidate,layerId));
        if (!target) return ii::core::fail(ErrorCode::failed,"Selected layer disappeared during adjustment");
        target->content.assetId=std::move(newAsset);recordDocumentChange(*candidate);
        const auto checked=validate(*candidate);
        if (!checked.ok()) return ii::core::fail(ErrorCode::invalid_argument,checked.issues.front().message);
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"Light adjustment was cancelled");
        return PreparedLightAdjustment{std::move(source),std::move(candidate),generation,revision,std::move(layerId),frame};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot prepare light adjustment",std::current_exception());}
}
}
