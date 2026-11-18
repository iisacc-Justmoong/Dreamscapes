#include "NativeLightPanel.h"
#include "App/ViewModels/Native/ViewValue.h"
#include <Render/FrameRenderer.h>
#include <iiJson>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <variant>

namespace dreamscapes::editor {
using ii::core::ErrorCode;
namespace {
using namespace iiSharedCanvas;
struct Preview final {PreparedLightAdjustment prepared;std::shared_ptr<const ii::gui::RasterImage> image;};
struct Analysis final {
    std::shared_ptr<const Document> source;std::uint64_t generation,revision;
    std::string layer;FrameIndex frame;iiGraphicAdjust::DevelopLightOptions values;
};
using WorkResult=std::variant<Preview,Analysis>;
struct TargetStamp final {
    std::shared_ptr<const Document> source;
    std::uint64_t generation,revision;
    std::string layer;
    std::int64_t frame;
};
TargetStamp targetStamp(const native::DocumentSession &document) {
    return {document.snapshot(),document.documentGeneration.get(),document.revision.get(),
            document.selectedLayer.get(),document.frame.get()};
}
bool matchesTarget(const native::DocumentSession &document,const TargetStamp &target) {
    return document.snapshot()==target.source && document.documentGeneration.get()==target.generation &&
        document.revision.get()==target.revision && document.hasCurrentLayerSelection() &&
        document.selectedLayer.get()==target.layer && document.frame.get()==target.frame;
}
std::string number(double value) {
    char buffer[64];const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,6);
    return result.ec==std::errc{}?std::string(buffer,result.ptr):std::string{};
}
iiGraphicAdjust::DevelopLightOptions options(const std::vector<std::shared_ptr<LightField>> &fields) {
    iiGraphicAdjust::DevelopLightOptions result;
    for (const auto &field:fields) {
        const auto value=field->value.get();
        if (field->key=="exposure") result.exposure=value;
        else if (field->key=="contrast") result.contrast=value;
        else if (field->key=="highlights") result.highlights=value;
        else if (field->key=="shadows") result.shadows=value;
        else if (field->key=="whites") result.whites=value;
        else if (field->key=="blacks") result.blacks=value;
    }
    return result;
}
double optionValue(const iiGraphicAdjust::DevelopLightOptions &values,std::string_view key) {
    if (key=="exposure") return values.exposure;
    if (key=="contrast") return values.contrast;
    if (key=="highlights") return values.highlights;
    if (key=="shadows") return values.shadows;
    if (key=="whites") return values.whites;
    if (key=="blacks") return values.blacks;
    return std::numeric_limits<double>::quiet_NaN();
}
ii::core::Result<Preview> preparePreview(std::shared_ptr<const Document> source,std::uint64_t generation,
    std::uint64_t revision,std::string layer,FrameIndex frame,iiGraphicAdjust::DevelopLightOptions values,
    double width,double height,const std::shared_ptr<std::atomic_bool> &cancelled) {
    const auto canceled=[cancelled]{return cancelled->load(std::memory_order_relaxed);};
    auto prepared=prepareLightAdjustment(std::move(source),generation,revision,std::move(layer),frame,values,canceled);
    if (!prepared) return std::unexpected(prepared.error());
    if (canceled()) return ii::core::fail(ErrorCode::cancelled,"Light preview was cancelled");
    const auto region=documentViewRegion(*prepared->candidate);
    if (region.extent.width<=0 || region.extent.height<=0)
        return ii::core::fail(ErrorCode::invalid_argument,"The canvas has no visible area");
    constexpr double maximum=2048;
    const auto scale=std::min({1.0,maximum/region.extent.width,maximum/region.extent.height,
        std::max(1.0,width)/region.extent.width,std::max(1.0,height)/region.extent.height});
    const CanvasExtent output{std::max(1,static_cast<int>(std::floor(region.extent.width*scale))),
                              std::max(1,static_cast<int>(std::floor(region.extent.height*scale)))};
    const auto sampling=output==region.extent?RasterSampling::Nearest:RasterSampling::Smooth;
    auto frameResult=renderFrameRegion(*prepared->candidate,frame,region,output,sampling);
    if (!frameResult.ok()) return ii::core::fail(ErrorCode::failed,
        frameResult.message.empty()?"Cannot render the light preview":frameResult.message);
    if (canceled()) return ii::core::fail(ErrorCode::cancelled,"Light preview was cancelled");
    const auto &pixels=frameResult.pixels;
    if (pixels.width!=output.width || pixels.height!=output.height ||
        pixels.pixels.size()!=std::uint64_t(output.width)*output.height)
        return ii::core::fail(ErrorCode::failed,"The preview renderer returned invalid pixels");
    std::vector<std::uint8_t> rgba;rgba.reserve(pixels.pixels.size()*4);
    for (auto value:pixels.pixels) {
        rgba.push_back(static_cast<std::uint8_t>(value>>16));rgba.push_back(static_cast<std::uint8_t>(value>>8));
        rgba.push_back(static_cast<std::uint8_t>(value));rgba.push_back(static_cast<std::uint8_t>(value>>24));
    }
    auto image=ii::gui::rgba_image(static_cast<std::uint32_t>(output.width),static_cast<std::uint32_t>(output.height),std::move(rgba));
    if (!image) return std::unexpected(image.error());
    return Preview{std::move(*prepared),std::move(*image)};
}
}
struct LightPanelModel::Bridge final {
    struct Completion final {std::uint64_t id;ii::core::Result<WorkResult> result;};
    std::mutex mutex;std::optional<Completion> completion;
    ii::runtime::EventLoop::Sender sender;ii::runtime::TaskToken wake;bool closed=false;
};
LightPanelModel::LightPanelModel(std::shared_ptr<native::DocumentSession> document,ii::runtime::EventLoop::Sender sender,
    std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor,std::string_view catalog)
    :document_(std::move(document)),sender_(std::move(sender)),executor_(std::move(executor)) {
    if (!document_ || !executor_) throw std::invalid_argument("Light panel requires a document and worker");
    const auto parsed=ii::json::parse(catalog);
    if (!parsed) throw std::invalid_argument("Invalid light control catalog");
    const auto &groups=vm::array(*parsed);
    const auto group=std::ranges::find_if(groups,[](const auto &item){return vm::text(vm::get(item,"key"))=="color";});
    if (group==groups.end()) throw std::invalid_argument("Light controls are missing");
    const std::array<std::pair<std::string_view,std::string_view>,6> keys{{
        {"exposure","field-1"},{"contrast","field-4"},{"highlights","field-8"},
        {"shadows","field-12"},{"whites","field-16"},{"blacks","field-20"}}};
    std::vector<std::shared_ptr<LightField>> rows;
    for (const auto &[key,id]:keys) {
        const auto &entries=vm::array(vm::get(*group,"fields"));
        const auto found=std::ranges::find_if(entries,[&](const auto &item){return vm::text(vm::get(item,"id"))==id;});
        if (found==entries.end()) throw std::invalid_argument("Light control is missing");
        auto row=std::make_shared<LightField>();row->key=key;row->label=vm::text(vm::get(*found,"label"));
        row->minimum=vm::number(vm::get(*found,"minimum"),std::numeric_limits<double>::quiet_NaN());
        row->maximum=vm::number(vm::get(*found,"maximum"),std::numeric_limits<double>::quiet_NaN());
        row->step=vm::number(vm::get(*found,"step"),0);
        const auto bound=key=="exposure"?5.0:100.0;
        if (row->label.empty() || !std::isfinite(row->minimum) || !std::isfinite(row->maximum) ||
            !std::isfinite(row->step) || row->minimum < -bound || row->maximum > bound ||
            row->minimum>0 || row->maximum<0 || row->minimum>=row->maximum || row->step<=0)
            throw std::invalid_argument("Invalid light control range");
        const auto unit=vm::text(vm::get(*found,"unit"));
        row->range=number(row->minimum)+" … "+number(row->maximum)+(unit.empty()?std::string{}:" "+unit);
        rows.push_back(std::move(row));
    }
    (void)fields.set(std::move(rows));
}
std::shared_ptr<LightPanelModel> LightPanelModel::create(std::shared_ptr<native::DocumentSession> document,
    ii::runtime::EventLoop &loop,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor,std::string_view catalog) {
    auto model=std::shared_ptr<LightPanelModel>(new LightPanelModel(std::move(document),loop.sender(),std::move(executor),catalog));
    model->start();return model;
}
void LightPanelModel::start() {
    const std::weak_ptr<LightPanelModel> weak=shared_from_this();
    for (const auto &row:fields.get()) row->owner=weak;
    auto admitted=sender_.on_signal([weak]{if (auto self=weak.lock()) self->deliver();});
    if (!admitted) throw std::runtime_error("Cannot register light preview completion");
    wake_=admitted.token;bridge_=std::make_shared<Bridge>();bridge_->sender=sender_;bridge_->wake=wake_;
    generation_=document_->documentGeneration.observe([weak](auto,std::uint64_t){if (auto self=weak.lock()) self->invalidate(true);});
    selection_=document_->selectedLayer.observe([weak](const auto &,std::uint64_t){if (auto self=weak.lock()) self->invalidate(true);});
    revision_=document_->revision.observe([weak](auto,std::uint64_t){if (auto self=weak.lock()) self->invalidate(false);});
    frame_=document_->frame.observe([weak](auto,std::uint64_t){if (auto self=weak.lock()) self->invalidate(false);});
    updateAvailability();
}
LightPanelModel::~LightPanelModel() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
ii::core::Result<void> LightPanelModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Light panel requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Light panel is closed");
    return {};
}
std::shared_ptr<LightField> LightPanelModel::field(std::string_view key) const {
    const auto &rows=fields.get();const auto it=std::ranges::find_if(rows,[&](const auto &row){return row->key==key;});
    return it==rows.end()?nullptr:*it;
}
void LightPanelModel::updateAvailability() {
    if (!writable()) return;
    const auto *doc=document_->document();
    const auto *layer=doc && document_->hasCurrentLayerSelection()?findLayer(*doc,document_->selectedLayer.get()):nullptr;
    const auto *bitmap=layer?std::get_if<StaticBitmapLayer>(layer):nullptr;
    const bool supported=bitmap && findRasterAsset(*doc,bitmap->content.assetId);
    (void)layerName.set(layer?layerProperties(*layer).name:std::string{});
    (void)availability.set(!doc?"Open a canvas first":!supported?"Select a static bitmap layer":std::string{});
    (void)available.set(active_ && visible_ && supported);
    const bool pending=std::ranges::any_of(fields.get(),[](const auto &row){return row->pending_ || row->invalid.get();});
    const bool changed=std::ranges::any_of(fields.get(),[](const auto &row){return row->value.get()!=0;});
    (void)canPreview.set(available.get() && !busy.get() && !pending && changed);
    (void)canApply.set(available.get() && !busy.get() && !pending && bool(prepared_) && bool(previewImage.get()));
}
void LightPanelModel::retirePrepared() {
    if (!prepared_) return;
    auto retired=std::move(*prepared_);prepared_.reset();
    if (executor_) try {executor_->post([retired=std::move(retired)] {});} catch (...) {}
}
void LightPanelModel::invalidate(bool resetValues) {
    if (!writable()) return;
    if (epoch_==std::numeric_limits<std::uint64_t>::max()) {(void)status.set("Light preview request limit reached");return;}
    const auto epoch=++epoch_;if (cancellation_) cancellation_->store(true,std::memory_order_relaxed);
    retirePrepared();(void)previewImage.set({});if (!active_ || epoch!=epoch_) return;
    if (resetValues) for (const auto &row:fields.get()) {
        row->pending_=false;(void)row->value.set(0);if (!active_ || epoch!=epoch_) return;
        (void)row->text.set("0");if (!active_ || epoch!=epoch_) return;
        (void)row->invalid.set(false);
        if (!active_ || epoch!=epoch_) return;
    }
    (void)status.set(busy.get()?"Cancelling preview…":std::string{});
    if (active_ && epoch==epoch_) updateAvailability();
}
void LightPanelModel::setVisible(bool value) {
    if (!writable() || visible_==value) return;visible_=value;
    if (!value) invalidate(false);else updateAvailability();
}
void LightPanelModel::setViewport(double w,double h) {
    if (!writable() || !std::isfinite(w) || !std::isfinite(h) || w<=0 || h<=0 || w>8192 || h>8192 ||
        (w==viewportWidth_ && h==viewportHeight_)) return;
    viewportWidth_=w;viewportHeight_=h;invalidate(false);
}
void LightPanelModel::sizeChanged(double w,double h,double inset,double valueWidth,double rowHeight,double rowPadding) {
    if (!writable() || !std::isfinite(w) || !std::isfinite(h) || !std::isfinite(inset) || !std::isfinite(valueWidth) ||
        !std::isfinite(rowHeight) || !std::isfinite(rowPadding) || w<0 || h<0 || w>8192 || h>8192 ||
        inset<0 || valueWidth<0 || rowHeight<=0 || rowPadding<0) return;
    (void)width.set(w);(void)height.set(h);
    const auto content=std::max(1.0,w-inset);(void)contentWidth.set(content);
    (void)sliderWidth.set(std::max(1.0,content-valueWidth));
    (void)listHeight.set(std::max(1.0,(rowHeight+rowPadding)*fields.get().size()));
}
ii::core::Result<void> LightPanelModel::setField(LightField &row,double value) {
    if (auto valid=writable();!valid) return valid;
    if (!available.get() || busy.get()) return ii::core::fail(ErrorCode::unsupported,"Light adjustment is unavailable");
    if (!std::isfinite(value) || value<row.minimum || value>row.maximum)
        return ii::core::fail(ErrorCode::invalid_argument,"Light value is outside its supported range");
    if (epoch_==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Light request limit reached");
    const auto target=targetStamp(*document_);const auto before=epoch_;
    invalidate(false);const auto epoch=epoch_;
    if (!active_ || !available.get() || busy.get() || inFlight_ || epoch!=before+1 || !matchesTarget(*document_,target))
        return ii::core::fail(ErrorCode::cancelled,"The canvas or layer changed");
    row.pending_=false;(void)row.value.set(value);
    if (!active_ || epoch!=epoch_) return ii::core::fail(ErrorCode::cancelled,"The canvas or layer changed");
    (void)row.text.set(number(value));
    if (!active_ || epoch!=epoch_) return ii::core::fail(ErrorCode::cancelled,"The canvas or layer changed");
    (void)row.invalid.set(false);
    if (active_ && epoch==epoch_) updateAvailability();
    if (!active_ || epoch!=epoch_) return ii::core::fail(ErrorCode::cancelled,"The canvas or layer changed");
    return {};
}
void LightPanelModel::editField(LightField &row,std::string value) {
    if (!writable() || !available.get() || busy.get()) return;
    if (value==row.text.get()) return;
    if (epoch_==std::numeric_limits<std::uint64_t>::max()) return;
    const auto target=targetStamp(*document_);const auto before=epoch_;
    invalidate(false);const auto epoch=epoch_;
    if (!active_ || !available.get() || busy.get() || inFlight_ || epoch!=before+1 || !matchesTarget(*document_,target)) return;
    row.pending_=true;(void)row.text.set(std::move(value));if (!active_ || epoch!=epoch_) return;
    (void)row.invalid.set(false);
    if (active_ && epoch==epoch_) updateAvailability();
}
void LightPanelModel::submitField(LightField &row) {
    if (!writable() || !available.get() || busy.get() || !row.pending_) return;
    const auto text=row.text.get();double value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if (parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value) ||
        value<row.minimum || value>row.maximum) {
        (void)row.invalid.set(true);(void)status.set("Enter a value within the displayed range");updateAvailability();return;
    }
    (void)setField(row,value);
}
ii::core::Result<void> LightField::setValue(double value) {
    if (auto model=owner.lock()) return model->setField(*this,value);
    return ii::core::fail(ErrorCode::closed,"Light panel is closed");
}
void LightField::edited(std::string value) {if (auto model=owner.lock()) model->editField(*this,std::move(value));}
void LightField::submitted() {if (auto model=owner.lock()) model->submitField(*this);}
void LightPanelModel::reset() {if (writable()) invalidate(true);}
ii::core::Result<void> LightPanelModel::requestAuto() {
    if (auto valid=writable();!valid) return valid;
    if (!available.get() || busy.get() || inFlight_) return ii::core::fail(ErrorCode::unsupported,"Light analysis is unavailable");
    if (epoch_==std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ErrorCode::out_of_range,"Light request limit reached");
    const auto target=targetStamp(*document_);const auto before=epoch_;
    invalidate(false);
    if (!active_ || !available.get() || !visible_ || busy.get() || inFlight_ || epoch_!=before+1 || !matchesTarget(*document_,target))
        return ii::core::fail(ErrorCode::cancelled,"The canvas or layer changed");
    if (epoch_==std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ErrorCode::out_of_range,"Light request limit reached");
    auto source=document_->snapshot();const auto generation=document_->documentGeneration.get(),revision=document_->revision.get();
    auto layer=document_->selectedLayer.get();const auto frame=static_cast<FrameIndex>(document_->frame.get());
    auto bridge=bridge_;std::shared_ptr<std::atomic_bool> cancellation;
    try {cancellation=std::make_shared<std::atomic_bool>(false);}
    catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot schedule light analysis",std::current_exception());}
    const auto id=++epoch_;cancellation_=cancellation;inFlight_=id;
    (void)busy.set(true);(void)status.set("Analyzing light…");updateAvailability();
    if (!active_ || id!=epoch_ || !visible_ || !executor_) {
        cancellation->store(true,std::memory_order_relaxed);cancellation_.reset();inFlight_=0;
        (void)busy.set(false);if (active_) updateAvailability();
        return ii::core::fail(ErrorCode::cancelled,"The canvas or layer changed");
    }
    try {
        executor_->post([source=std::move(source),generation,revision,layer=std::move(layer),frame,bridge,cancellation,id] {
            ii::core::Result<WorkResult> result=ii::core::fail(ErrorCode::failed,"Cannot analyze light");
            try {
                auto values=analyzeLightAdjustment(source,layer,frame,[cancellation]{return cancellation->load(std::memory_order_relaxed);});
                if (values) result=WorkResult{Analysis{source,generation,revision,layer,frame,*values}};
                else result=std::unexpected(values.error());
            } catch (...) {result=ii::core::fail(ErrorCode::failed,"Cannot analyze light",std::current_exception());}
            {std::lock_guard lock(bridge->mutex);if (bridge->closed) return;bridge->completion.emplace(Bridge::Completion{id,std::move(result)});}
            (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
        });
    } catch (...) {
        cancellation_.reset();inFlight_=0;(void)busy.set(false);(void)status.set("Light analysis worker is unavailable");updateAvailability();
        return ii::core::fail(ErrorCode::failed,status.get(),std::current_exception());
    }
    return {};
}
ii::core::Result<void> LightPanelModel::requestPreview() {
    if (auto valid=writable();!valid) return valid;
    if (!canPreview.get() || inFlight_) return ii::core::fail(ErrorCode::unsupported,"Light preview is unavailable");
    if (epoch_==std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ErrorCode::out_of_range,"Light preview request limit reached");
    auto source=document_->snapshot();const auto generation=document_->documentGeneration.get(),revision=document_->revision.get();
    auto layer=document_->selectedLayer.get();const auto frame=static_cast<iiSharedCanvas::FrameIndex>(document_->frame.get());
    const auto values=options(fields.get());const auto width=viewportWidth_,height=viewportHeight_;
    auto bridge=bridge_;std::shared_ptr<std::atomic_bool> cancellation;
    try {cancellation=std::make_shared<std::atomic_bool>(false);}
    catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot schedule the light preview",std::current_exception());}
    const auto id=++epoch_;
    cancellation_=cancellation;inFlight_=id;(void)busy.set(true);(void)status.set("Preparing light preview…");updateAvailability();
    if (!active_ || id!=epoch_ || !visible_ || !executor_) {
        cancellation->store(true,std::memory_order_relaxed);cancellation_.reset();inFlight_=0;
        (void)busy.set(false);if (active_) updateAvailability();
        return ii::core::fail(ErrorCode::cancelled,"The canvas or layer changed");
    }
    try {
        executor_->post([source=std::move(source),generation,revision,layer=std::move(layer),frame,values,width,height,bridge,cancellation,id] {
            ii::core::Result<WorkResult> result=ii::core::fail(ErrorCode::failed,"Cannot prepare the light preview");
            try {
                auto preview=preparePreview(source,generation,revision,layer,frame,values,width,height,cancellation);
                if (preview) result=WorkResult{std::move(*preview)};
                else result=std::unexpected(preview.error());
            }
            catch (...) {result=ii::core::fail(ErrorCode::failed,"Cannot prepare the light preview",std::current_exception());}
            {std::lock_guard lock(bridge->mutex);if (bridge->closed) return;bridge->completion.emplace(Bridge::Completion{id,std::move(result)});}
            (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
        });
    } catch (...) {
        cancellation_.reset();inFlight_=0;(void)busy.set(false);(void)status.set("Light preview worker is unavailable");updateAvailability();
        return ii::core::fail(ErrorCode::failed,status.get(),std::current_exception());
    }
    return {};
}
void LightPanelModel::deliver() {
    if (!writable()) return;
    std::optional<Bridge::Completion> completion;
    {std::lock_guard lock(bridge_->mutex);completion=std::move(bridge_->completion);bridge_->completion.reset();}
    if (!completion || completion->id!=inFlight_) return;
    inFlight_=0;cancellation_.reset();const auto id=completion->id;
    if (id==epoch_ && visible_) {
        if (completion->result) {
            auto matches=[&](const auto &source,auto generation,auto revision,const auto &layer,auto frame) {
                return document_->snapshot()==source && document_->documentGeneration.get()==generation &&
                    document_->revision.get()==revision && document_->hasCurrentLayerSelection() &&
                    document_->selectedLayer.get()==layer && document_->frame.get()==frame;
            };
            if (auto *value=std::get_if<Preview>(&*completion->result)) {
                if (matches(value->prepared.source,value->prepared.generation,value->prepared.revision,
                            value->prepared.layerId,value->prepared.frame)) {
                    prepared_=std::move(value->prepared);(void)previewImage.set(std::move(value->image));
                    if (active_ && id==epoch_) (void)status.set("Preview ready");
                } else (void)status.set("The canvas or layer changed");
            } else {
                const auto &analysis=std::get<Analysis>(*completion->result);
                if (!matches(analysis.source,analysis.generation,analysis.revision,analysis.layer,analysis.frame)) {
                    (void)status.set("The canvas or layer changed");
                } else {
                    std::vector<double> values;values.reserve(fields.get().size());
                    for (const auto &row:fields.get()) {
                        const auto raw=optionValue(analysis.values,row->key);
                        if (!std::isfinite(raw)) {values.clear();break;}
                        values.push_back(std::clamp(std::round(raw/row->step)*row->step,row->minimum,row->maximum));
                    }
                    if (values.size()!=fields.get().size()) (void)status.set("Light analysis returned invalid values");
                    else {
                        for (std::size_t i=0;i<values.size();++i) {
                            auto &row=*fields.get()[i];row.pending_=false;(void)row.value.set(values[i]);
                            if (!active_ || id!=epoch_) break;
                            (void)row.text.set(number(values[i]));if (!active_ || id!=epoch_) break;
                            (void)row.invalid.set(false);if (!active_ || id!=epoch_) break;
                        }
                        if (active_ && id==epoch_) (void)status.set("Auto light values ready");
                    }
                }
            }
        } else (void)status.set(completion->result.error().message);
    } else (void)status.set({});
    if (!active_) return;
    (void)busy.set(false);if (active_) updateAvailability();
}
ii::core::Result<void> LightPanelModel::apply() {
    if (auto valid=writable();!valid) return valid;
    if (!canApply.get() || !prepared_) return ii::core::fail(ErrorCode::unsupported,"Prepare the light preview first");
    auto value=std::move(*prepared_);prepared_.reset();
    auto result=document_->commitLightAdjustment(std::move(value));
    if (!result) {(void)status.set(result.error().message);updateAvailability();return result;}
    if (active_) {invalidate(true);(void)status.set("Light adjustment applied");}
    return {};
}
void LightPanelModel::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Light panel requires its owner thread");
    if (!active_) return;active_=false;visible_=false;
    generation_={};revision_={};selection_={};frame_={};
    if (cancellation_) cancellation_->store(true,std::memory_order_relaxed);
    retirePrepared();(void)previewImage.set({});
    if (bridge_) {std::lock_guard lock(bridge_->mutex);bridge_->closed=true;bridge_->completion.reset();}
    (void)sender_.cancel(wake_);executor_.reset();(void)busy.set(false);(void)available.set(false);
    (void)canPreview.set(false);(void)canApply.set(false);
}
}
