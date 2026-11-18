#include "NativeBrushStroke.h"
#include <Document/CanvasSampling.h>
#include <Document/DocumentEditor.h>
#include <Media/MediaIo.h>
#include <Validation/Validation.h>
#include <limits>
#include <utility>

namespace dreamscapes::editor {
using namespace iiSharedCanvas;
using ii::core::ErrorCode;
ii::core::Result<std::unique_ptr<BrushStroke>> BrushStroke::create(std::shared_ptr<const Document> source,
    std::uint64_t generation,std::uint64_t revision,std::string layerId,FrameIndex frame,const BitmapBrush &brush) {
    if (!source) return ii::core::fail(ErrorCode::closed,"Open a canvas first");
    const auto *layer=findLayer(*source,layerId);
    if (!layer || (!std::holds_alternative<StaticBitmapLayer>(*layer) && !std::holds_alternative<DynamicBitmapLayer>(*layer)))
        return ii::core::fail(ErrorCode::unsupported,"Select a bitmap layer");
    if (!sampleLayerAt(*source,*layer,frame).visible)
        return ii::core::fail(ErrorCode::unsupported,"The selected layer is not visible at this frame");
    const auto *resolved=resolveAssetAt(*source,*layer,frame);
    const auto *raster=resolved?std::get_if<RasterAsset>(resolved):nullptr;
    if (!raster || raster->pixels.width<=0 || raster->pixels.height<=0)
        return ii::core::fail(ErrorCode::unsupported,"The selected frame does not contain a finite bitmap");
    const auto count=std::uint64_t(raster->pixels.width)*std::uint64_t(raster->pixels.height);
    const MediaLimits limits;
    if (count!=raster->pixels.pixels.size() || count>limits.maxPixelsPerFrame || count>limits.maxDecodedBytes/32)
        return ii::core::fail(ErrorCode::out_of_range,"The selected bitmap exceeds the brush memory limit");
    BitmapEditor validator;
    if (!validator.setBrush(brush)) return ii::core::fail(ErrorCode::invalid_argument,validator.lastError());
    try {
        auto result=std::unique_ptr<BrushStroke>(new BrushStroke);
        result->source_=std::move(source);result->generation_=generation;result->revision_=revision;
        result->layerId_=std::move(layerId);result->frame_=frame;result->sourceAsset_=raster->id;
        result->candidate_=std::make_shared<Document>(*result->source_);
        for (std::uint64_t i=1;i<std::numeric_limits<std::uint64_t>::max();++i) {
            auto id="dreamscapes.brush."+std::to_string(i);
            if (!findAsset(*result->candidate_,id) && !findAudioAsset(*result->candidate_,id)) {
                result->targetAsset_=std::move(id);break;
            }
        }
        if (result->targetAsset_.empty()) return ii::core::fail(ErrorCode::out_of_range,"Brush asset identity is exhausted");
        DocumentEditor document(*result->candidate_);
        DocumentEditResult inserted;
        if (std::holds_alternative<DynamicBitmapLayer>(*layer))
            inserted=document.setDynamicFrameContent(result->layerId_,frame,RasterAsset{result->targetAsset_,raster->pixels});
        else {
            inserted=document.insertRasterAsset(result->targetAsset_,raster->pixels);
            if (inserted.ok()) inserted=document.setStaticSource(result->layerId_,result->targetAsset_);
        }
        if (!inserted.ok()) return ii::core::fail(ErrorCode::invalid_argument,inserted.message);
        result->bitmap_=std::make_unique<BitmapEditor>(*result->candidate_,result->targetAsset_);
        if (!result->bitmap_->isBound() || !result->bitmap_->setBrush(brush))
            return ii::core::fail(ErrorCode::failed,result->bitmap_->lastError());
        return result;
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot prepare the brush stroke",std::current_exception());}
}
BrushStroke::~BrushStroke() {cancel();}
ii::core::Result<void> BrushStroke::begin(const StrokePoint &point) {
    if (!bitmap_ || active_ || finished_) return ii::core::fail(ErrorCode::closed,"Brush stroke is unavailable");
    if (!bitmap_->beginStrokeInput(point)) {
        const auto message=bitmap_->lastError();cancel();return ii::core::fail(ErrorCode::invalid_argument,message);
    }
    active_=true;return {};
}
ii::core::Result<void> BrushStroke::move(const StrokePoint &point) {
    if (!bitmap_ || !active_ || finished_) return ii::core::fail(ErrorCode::closed,"No brush stroke is active");
    if (!bitmap_->continueStrokeInput(point)) {
        const auto message=bitmap_->lastError();cancel();return ii::core::fail(ErrorCode::failed,message);
    }
    return {};
}
ii::core::Result<PreparedBrushStroke> BrushStroke::finish(const StrokePoint &point) {
    if (!bitmap_ || !active_ || finished_) return ii::core::fail(ErrorCode::closed,"No brush stroke is active");
    if (!bitmap_->endStrokeInput(point)) {
        const auto message=bitmap_->lastError();cancel();return ii::core::fail(ErrorCode::failed,message);
    }
    active_=false;finished_=true;
    const auto *before=findRasterAsset(*source_,sourceAsset_);
    const auto *after=findRasterAsset(*candidate_,targetAsset_);
    if (!before || !after || before->pixels.pixels==after->pixels.pixels)
        return ii::core::fail(ErrorCode::invalid_argument,"The brush did not change any pixels");
    const auto valid=validate(*candidate_);
    if (!valid.ok()) return ii::core::fail(ErrorCode::invalid_argument,valid.issues.front().message);
    bitmap_.reset();
    return PreparedBrushStroke{std::move(source_),std::move(candidate_),generation_,revision_,std::move(layerId_),frame_};
}
void BrushStroke::cancel() noexcept {
    if (bitmap_ && active_) try {bitmap_->cancelStroke();} catch (...) {}
    active_=false;finished_=true;bitmap_.reset();candidate_.reset();source_.reset();
}
}
