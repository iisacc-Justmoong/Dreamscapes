#include "NativeTextTool.h"
#include "App/ViewModels/Native/ViewValue.h"
#include <iiColorManager>
#include <iiJson>
#include <iiSDKCore>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dreamscapes::editor {
namespace {
using ii::core::ErrorCode;
std::optional<std::uint32_t> parseColor(std::string_view value) {
    const auto parsed=iiColorManager::parseHexValue(std::string(value));if (!parsed) return {};
    const auto byte=[](double channel){return static_cast<std::uint32_t>(std::lround(std::clamp(channel,0.0,1.0)*255));};
    return (byte(parsed->a)<<24)|(byte(parsed->r)<<16)|(byte(parsed->g)<<8)|byte(parsed->b);
}
bool validText(std::string_view text) {return text.size()<=32000 && text.find('\0')==text.npos && ii::core::valid_utf8(text);}
}
ii::core::Result<TextDefaults> parseTextDefaults(std::string_view source) {
    const auto parsed=ii::json::parse(source);
    if (!parsed) return ii::core::fail(ErrorCode::invalid_argument,"Cannot read text defaults");
    const auto &tools=vm::array(*parsed);
    const auto tool=std::ranges::find_if(tools,[](const auto &entry){return vm::text(vm::get(entry,"key"))=="text";});
    if (tool==tools.end()) return ii::core::fail(ErrorCode::invalid_argument,"Text defaults are missing");
    const auto &fields=vm::array(vm::get(*tool,"fields"));
    auto field=[&](std::string_view id)->const vm::Value & {
        auto item=std::ranges::find_if(fields,[&](const auto &entry){return vm::text(vm::get(entry,"id"))==id;});
        return item==fields.end()?vm::get(vm::Value{},""):*item;
    };
    const auto &size=field("field-2");
    TextDefaults result;
    result.family=vm::text(vm::get(field("field-1"),"initial"));
    result.placeholder=vm::text(vm::get(field("field-0"),"initial"));
    result.colorText=vm::text(vm::get(field("textCustomFill"),"initial"));
    result.size=vm::number(vm::get(size,"initial"),-1);result.minimum=vm::number(vm::get(size,"minimum"),-1);
    result.maximum=vm::number(vm::get(size,"maximum"),-1);result.step=vm::number(vm::get(size,"step"),-1);
    const auto parsedColor=parseColor(result.colorText);
    if (!parsedColor || result.family.empty() || !validText(result.family) || !std::isfinite(result.size) ||
        !std::isfinite(result.minimum) || !std::isfinite(result.maximum) || !std::isfinite(result.step) ||
        result.minimum<=0 || result.maximum<result.minimum || result.step<=0 || result.size<result.minimum || result.size>result.maximum)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid text defaults");
    result.color=*parsedColor;return result;
}
TextToolModel::TextToolModel(std::shared_ptr<native::DocumentSession> document,TextDefaults config)
    :defaults(std::move(config)),family(defaults.family),colorText(defaults.colorText),fontSize(defaults.size),
     document_(std::move(document)),outliner_(nativeTextOutline),color_(defaults.color) {
    if (!document_) throw std::invalid_argument("Text tool requires a document");
}
std::shared_ptr<TextToolModel> TextToolModel::create(std::shared_ptr<native::DocumentSession> document,TextDefaults defaults) {
    auto value=std::shared_ptr<TextToolModel>(new TextToolModel(std::move(document),std::move(defaults)));value->start();return value;
}
void TextToolModel::start() {
    const std::weak_ptr<TextToolModel> weak=shared_from_this();
    generationObserver_=document_->documentGeneration.observe([weak](const auto &,std::uint64_t){
        if (auto self=weak.lock()) {self->cancel();self->frame_.reset();}
    });
    revisionObserver_=document_->revision.observe([weak](auto revision,std::uint64_t){
        if (auto self=weak.lock();self && self->gesture_ && self->gesture_->revision!=revision) self->cancel();
    });
    frameObserver_=document_->frame.observe([weak](const auto &,std::uint64_t){
        if (auto self=weak.lock()) self->cancel();
    });
}
ii::core::Result<void> TextToolModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Text tool requires its owner thread");return {};
}
ii::core::Result<void> TextToolModel::setContent(std::string value) {
    if (auto ok=writable();!ok) return ok;
    if (!validText(value)) return ii::core::fail(ErrorCode::invalid_argument,"Text must be valid UTF-8 and at most 32000 bytes");
    cancel();(void)content.set(std::move(value));return {};
}
ii::core::Result<void> TextToolModel::setFamily(std::string value) {
    if (auto ok=writable();!ok) return ok;
    if (value.empty() || value.size()>256 || !validText(value)) return ii::core::fail(ErrorCode::invalid_argument,"Enter a valid font family");
    cancel();(void)family.set(std::move(value));return {};
}
ii::core::Result<void> TextToolModel::setFontSize(double value) {
    if (auto ok=writable();!ok) return ok;
    if (!std::isfinite(value) || value<defaults.minimum || value>defaults.maximum)
        return ii::core::fail(ErrorCode::invalid_argument,"Font size is outside its supported range");
    cancel();(void)fontSize.set(value);return {};
}
ii::core::Result<void> TextToolModel::setColor(std::string value) {
    if (auto ok=writable();!ok) return ok;
    const auto parsed=parseColor(value);if (!parsed) return ii::core::fail(ErrorCode::invalid_argument,"Enter a valid text color");
    cancel();color_=*parsed;(void)colorText.set(std::move(value));return {};
}
void TextToolModel::setOutliner(TextOutliner value) {if (writable()) {cancel();outliner_=std::move(value);}}
void TextToolModel::setVisible(bool value) {if (writable()) {visible_=value;if (!value) cancel();}}
void TextToolModel::setViewport(double w,double h) {
    if (!writable() || !std::isfinite(w) || !std::isfinite(h) || w<=0 || h<=0 || w>8192 || h>8192) return;
    if (w!=width.get() || h!=height.get()) cancel();(void)width.set(w);(void)height.set(h);
}
void TextToolModel::setFrame(std::shared_ptr<const ii::gui::RasterImage> value) {
    if (!writable()) return;
    if (!value || !frame_ || value->width()!=frame_->width() || value->height()!=frame_->height()) cancel();
    frame_=std::move(value);
}
void TextToolModel::cancel() {if (writable()) gesture_.reset();}
ii::core::Result<bool> TextToolModel::input(const ii::gui::WidgetInput &input) {
    if (auto valid=writable();!valid) return std::unexpected(valid.error());
    if (input.local_position && (!std::isfinite(input.local_position->x) || !std::isfinite(input.local_position->y)))
        return ii::core::fail(ErrorCode::invalid_argument,"Text pointer coordinates must be finite");
    using Kind=ii::gui::WidgetInputKind;
    if (input.kind==Kind::focus_lost || (input.kind==Kind::key_down && input.key==ii::gui::WidgetKey::escape)
#if defined(IIGUI_POINTER_CANCEL_API)
        || input.kind==Kind::pointer_cancel
#endif
    ) {const bool consumed=gesture_.has_value();cancel();return consumed;}
    if (!visible_ || !frame_ || !document_->document()) return false;
    if (input.kind==Kind::pointer_down) {
        if (gesture_ || (input.buttons && !(input.buttons&1)) || !input.local_position) return false;
        const auto region=documentViewRegion(*document_->document());
        if (region.extent.width<=0 || region.extent.height<=0 || frame_->width()<=0 || frame_->height()<=0) return false;
        const auto scale=std::min(width.get()/frame_->width(),height.get()/frame_->height());
        const auto dw=frame_->width()*scale,dh=frame_->height()*scale,left=(width.get()-dw)/2,top=(height.get()-dh)/2;
        const auto point=*input.local_position;
        if (!(scale>0) || point.x<left || point.y<top || point.x>left+dw || point.y>top+dh) return false;
        gesture_=Gesture{document_->documentGeneration.get(),document_->revision.get(),input.device_id,point.x,point.y,left,top,dw,dh,region};
        (void)status.set({});return true;
    }
    if (!gesture_ || input.device_id!=gesture_->device || input.kind!=Kind::pointer_up || !input.local_position) return false;
    const auto gesture=*gesture_;cancel();
    if (gesture.generation!=document_->documentGeneration.get() || gesture.revision!=document_->revision.get()) return true;
    if (content.get().empty()) {(void)status.set("Enter text before placing it on the canvas");return true;}
    if (!outliner_) {(void)status.set("Text outlines are unavailable on this platform");return true;}
    iiSharedCanvas::Point origin{gesture.region.origin.x+(gesture.x-gesture.left)*gesture.region.extent.width/gesture.displayWidth,
        gesture.region.origin.y+(gesture.y-gesture.top)*gesture.region.extent.height/gesture.displayHeight};
    TextOutlineRequest request{content.get(),family.get(),fontSize.get(),color_,origin,
        gesture.region.origin.x+gesture.region.extent.width-origin.x,gesture.region.origin.y+gesture.region.extent.height-origin.y};
    try {
        auto outline=outliner_(request);
        if (!outline) {(void)status.set(outline.error().message);return true;}
        if (gesture.generation!=document_->documentGeneration.get() || gesture.revision!=document_->revision.get()) return true;
        auto result=document_->insertVectorDrawing("Text",std::move(*outline));
        (void)status.set(result?std::string{}:result.error().message);
    } catch (...) {(void)status.set("Cannot create text outlines");}
    return true;
}
}
