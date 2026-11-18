#include "NativeBackgroundPanel.h"
#include "App/Native/NativeCanvasFiles.h"
#include "App/ViewModels/Native/ViewValue.h"
#include <Media/MediaIo.h>
#include <Render/FrameRenderer.h>
#include <iiJson>
#include <LVUI>
#include <iiSDKCore>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dreamscapes::editor {
namespace {
using ii::core::ErrorCode;
using Image=std::shared_ptr<const ii::gui::RasterImage>;
struct SurfaceResult {std::uint64_t version=0;Image plane,rail;std::string error;};
bool percent(double value) {return std::isfinite(value) && value>=0 && value<=100;}
std::string number(double value) {
    char buffer[64];const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,6);
    return result.ec==std::errc{}?std::string(buffer,result.ptr):std::string{};
}
std::string colorText(iiColorManager::Rgba color) {return iiColorManager::toHex(color,color.a<1);}
iiColorManager::Rgba requiredColor(const vm::Value &value) {
    auto color=iiColorManager::parseHex(vm::text(value));
    if (!color) throw std::invalid_argument("Invalid background color catalog");
    return *color;
}
}
struct BackgroundPanelModel::Bridge final {
    std::mutex mutex;
    std::optional<SurfaceResult> surface;
    std::optional<ii::core::Result<PreparedBackgroundFill>> fill;
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    bool closed=false;
};
BackgroundPanelModel::BackgroundPanelModel(std::shared_ptr<native::DocumentSession> document,
    ii::runtime::EventLoop::Sender sender,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor,std::string_view catalog)
    :document_(std::move(document)),sender_(std::move(sender)),executor_(std::move(executor)) {
    if (!document_ || !executor_) throw std::invalid_argument("Background panel requires a document and worker");
    const lvui::SliderConfiguration slider;
    (void)width.set(slider.width);(void)contentWidth.set(slider.width);(void)channelSliderWidth.set(slider.width);
    (void)markerSize.set(slider.range_inset);
    const auto parsed=ii::json::parse(catalog);
    if (!parsed) throw std::invalid_argument("Invalid background defaults");
    const auto &color=vm::get(*parsed,"color");
    defaults_.color=requiredColor(vm::get(color,"backgroundColor"));
    const auto a=vm::number(vm::get(color,"backgroundAlpha"),-1),o=vm::number(vm::get(color,"backgroundOpacity"),-1);
    if (!percent(a) || !percent(o)) throw std::invalid_argument("Invalid background alpha or opacity defaults");
    defaults_.alpha=a/100;defaults_.opacity=o/100;
    defaults_.angleDegrees=vm::number(vm::get(color,"backgroundAngle"),90);
    defaults_.perceptual=vm::text(vm::get(color,"backgroundInterpolation"))=="Perceptual";
    for (const auto &entry:vm::array(vm::get(color,"backgroundGradientStops"))) {
        const auto position=vm::number(vm::get(entry,"position"),-1);
        if (!std::isfinite(position) || position<0 || position>1) throw std::invalid_argument("Invalid gradient stop default");
        defaults_.stops.push_back({position,requiredColor(vm::get(entry,"color"))});
    }
    if (defaults_.stops.size()<2 || defaults_.stops.size()>32 || !std::isfinite(defaults_.angleDegrees) ||
        defaults_.angleDegrees < -360 || defaults_.angleDegrees > 360) throw std::invalid_argument("Invalid gradient defaults");
    if (vm::text(vm::get(color,"backgroundFill"))=="Gradient")
        defaults_.style=vm::text(vm::get(color,"backgroundGradientType"))=="Radial"?BackgroundFillStyle::Radial:BackgroundFillStyle::Linear;
    const auto modes=iiColorManager::modes();
    const auto mode=vm::number(vm::get(color,"backgroundPickerMode"),-1);
    if (!std::isfinite(mode) || mode<0 || std::floor(mode)!=mode || mode>=static_cast<double>(modes.size()))
        throw std::invalid_argument("Invalid background picker default");
    defaultMode_=modes[static_cast<std::size_t>(mode)].mode;
    for (const auto &entry:modes) modeNames.emplace_back(entry.name);
    for (const auto &entry:vm::array(vm::get(*parsed,"presets"))) {
        Preset preset; preset.name=vm::text(vm::get(entry,"name"));
        for (const auto &item:vm::array(vm::get(entry,"colors"))) preset.colors.push_back(requiredColor(item));
        if (preset.name.empty() || preset.colors.size()<2 || preset.colors.size()>32)
            throw std::invalid_argument("Invalid background gradient preset");
        presetNames.push_back(preset.name);
        for (const auto colorValue:preset.colors) {
            const auto name=colorText(colorValue);
            if (std::ranges::find(swatchNames,name)==swatchNames.end()) {swatchNames.push_back(name);swatches_.push_back(colorValue);}
        }
        presets_.push_back(std::move(preset));
    }
}
std::shared_ptr<BackgroundPanelModel> BackgroundPanelModel::create(std::shared_ptr<native::DocumentSession> document,
    ii::runtime::EventLoop &loop,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor,std::string_view catalog) {
    auto result=std::shared_ptr<BackgroundPanelModel>(new BackgroundPanelModel(std::move(document),loop.sender(),std::move(executor),catalog));
    result->start();return result;
}
void BackgroundPanelModel::start() {
    const std::weak_ptr<BackgroundPanelModel> weak=shared_from_this();
    auto admitted=sender_.on_signal([weak]{if (auto self=weak.lock()) self->deliver();});
    if (!admitted) throw std::runtime_error("Cannot register background completion");
    wake_=admitted.token;bridge_=std::make_shared<Bridge>();bridge_->sender=sender_;bridge_->wake=wake_;
    documentObserver_=document_->documentGeneration.observe([weak](auto,std::uint64_t) {
        if (auto self=weak.lock()) {
            if (self->fillCancellation_) self->fillCancellation_->store(true,std::memory_order_relaxed);
            self->resetDraft();self->updateAvailability();
        }
    });
    revisionObserver_=document_->revision.observe([weak](auto,std::uint64_t){if (auto self=weak.lock()) {
        if (self->fillCancellation_) self->fillCancellation_->store(true,std::memory_order_relaxed);
        self->updateAvailability();
    }});
    resetDraft();updateAvailability();
}
BackgroundPanelModel::~BackgroundPanelModel() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
ii::core::Result<void> BackgroundPanelModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Background panel requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Background panel is closed");
    return {};
}
void BackgroundPanelModel::resetDraft() {
    imagePath_.clear();(void)imageName.set("No image selected");(void)imageFit.set(0);
    draft_=defaults_;(void)picker_.setMode(defaultMode_);
    const auto modes=iiColorManager::modes();
    const auto mode=std::ranges::find_if(modes,[&](const auto &entry){return entry.mode==defaultMode_;});
    (void)modeIndex.set(static_cast<int>(mode-modes.begin()));
    (void)fillIndex.set(draft_.style==BackgroundFillStyle::Solid?0:draft_.style==BackgroundFillStyle::Linear?1:2);
    (void)stopIndex.set(0);(void)alpha.set(draft_.alpha*100);(void)opacity.set(draft_.opacity*100);
    (void)angle.set(draft_.angleDegrees);(void)perceptual.set(draft_.perceptual);
    (void)alphaText.set(number(alpha.get())+"%");(void)opacityText.set(number(opacity.get())+"%");
    (void)angleText.set(number(angle.get())+"°");
    (void)gradient.set(draft_.style!=BackgroundFillStyle::Solid);
    (void)picker_.begin(draft_.style==BackgroundFillStyle::Solid?draft_.color:draft_.stops.front().color);
    hexPending_=false;(void)status.set({});projectStops();projectPicker(true);
}
void BackgroundPanelModel::showColor() {if (!writable()) return;(void)replaceMode.set(false);updateAvailability();requestSurface();}
void BackgroundPanelModel::showReplace() {if (!writable()) return;(void)replaceMode.set(true);updateAvailability();requestSurface();}
void BackgroundPanelModel::setImagePickHandler(std::function<ii::core::Result<void>()> value) {
    if (!writable()) return;pickImage_=std::move(value);updateAvailability();
}
ii::core::Result<void> BackgroundPanelModel::chooseImage() {
    if (auto valid=writable();!valid) return valid;
    if (!canChooseImage.get() || !pickImage_) return ii::core::fail(ErrorCode::unsupported,"Image picker is unavailable");
    auto picker=pickImage_;
    try {auto result=picker();if (!result) (void)status.set(result.error().message);return result;}
    catch (...) {(void)status.set("Cannot open the image picker");return ii::core::fail(ErrorCode::failed,status.get(),std::current_exception());}
}
ii::core::Result<void> BackgroundPanelModel::setImagePath(std::filesystem::path path) {
    if (auto valid=writable();!valid) return valid;
    if (busy.get() || !native::supportsCanvasInput(path))
        return ii::core::fail(ErrorCode::invalid_argument,"Choose a supported local image or canvas file");
    const auto name=ii::core::path_to_utf8(path.filename());
    if (!name || name->empty()) return ii::core::fail(ErrorCode::invalid_argument,"Image filename is invalid");
    imagePath_=std::move(path);(void)imageName.set(*name);(void)status.set({});updateAvailability();return {};
}
ii::core::Result<void> BackgroundPanelModel::selectImageFit(int value) {
    if (auto valid=writable();!valid) return valid;
    if (busy.get() || value<0 || value>=static_cast<int>(imageFits.size()))
        return ii::core::fail(ErrorCode::invalid_argument,"Unknown background image fit");
    (void)imageFit.set(value);return {};
}
void BackgroundPanelModel::reportError(std::string value) {if (writable()) (void)status.set(std::move(value));}
void BackgroundPanelModel::setVisible(bool value) {
    if (!writable() || visible_==value) return;
    visible_=value;updateAvailability();requestSurface();
}
void BackgroundPanelModel::sizeChanged(double w,double h,double inset,double rail,double gap,double rowHeight) {
    if (!std::isfinite(w)||!std::isfinite(h)||!std::isfinite(inset)||!std::isfinite(rail)||!std::isfinite(gap)||!std::isfinite(rowHeight)||
        w<0||h<0||w>8192||h>8192||inset<0||rail<=0||gap<0||rowHeight<=0) return;
    const auto content=std::max(lvui::SliderConfiguration{}.range_inset*2,w-inset);
    const auto plane=std::max(1.0,content-rail-gap);
    (void)width.set(std::max(1.0,w));(void)height.set(std::max(1.0,h));(void)contentWidth.set(content);
    (void)railWidth.set(rail);(void)planeWidth.set(plane);(void)planeHeight.set(plane);
    (void)markerSize.set(std::max(1.0,gap));
    (void)channelSliderWidth.set(std::max(lvui::SliderConfiguration{}.range_inset*2,content-rowHeight-gap));
    rowHeight_=rowHeight;(void)channelListHeight.set(std::max(1.0,rowHeight_*channels.get().size()));
    (void)allChannelListHeight.set(std::max(1.0,rowHeight_*allChannels.get().size()));
}
void BackgroundPanelModel::updateAvailability() {
    const auto *doc=document_->document();
    const auto *layer=doc?iiSharedCanvas::findLayer(*doc,"canvas.background.layer"):nullptr;
    const bool supported=doc && doc->canvasMode==iiSharedCanvas::CanvasMode::Finite && doc->artboards.empty() &&
        (!layer || ((std::holds_alternative<iiSharedCanvas::StaticBitmapLayer>(*layer) ||
                     std::holds_alternative<iiSharedCanvas::StaticVectorLayer>(*layer)) &&
                    !iiSharedCanvas::layerProperties(*layer).artboardId && iiSharedCanvas::layerProperties(*layer).motion.empty()));
    (void)availability.set(!doc?"Open a canvas first":supported?std::string{}:
        "Background editing is unavailable for this canvas or layer");
    (void)canApply.set(active_ && visible_ && supported && !busy.get() && !hexPending_ && !replaceMode.get());
    (void)canChooseImage.set(active_ && visible_ && supported && !busy.get() && replaceMode.get() && bool(pickImage_));
    (void)canApplyImage.set(active_ && visible_ && supported && !busy.get() && replaceMode.get() && !imagePath_.empty());
}
void BackgroundPanelModel::projectStops() {
    std::vector<std::string> names;names.reserve(draft_.stops.size());
    for (std::size_t i=0;i<draft_.stops.size();++i)
        names.push_back("Stop "+std::to_string(i+1)+" · "+std::to_string(std::lround(draft_.stops[i].position*100))+"%");
    (void)stopNames.set(std::move(names));
    const auto selected=static_cast<std::size_t>(std::max(0,stopIndex.get()));
    if (selected<draft_.stops.size()) {
        (void)stopPosition.set(draft_.stops[selected].position*100);
        (void)stopPositionText.set(number(stopPosition.get())+"%");
    }
    (void)canRemoveStop.set(draft_.stops.size()>2);
}
void BackgroundPanelModel::projectPicker(bool rebuild) {
    if (gradient.get()) draft_.stops[static_cast<std::size_t>(stopIndex.get())].color=picker_.color();
    else draft_.color=picker_.color();
    (void)hex.set(colorText(picker_.color()));
    const auto info=iiColorManager::modeInfo(picker_.mode());
    if (!info) return;
    (void)hasPlane.set(info->hasPlane);
    if (rebuild) {
        std::vector<std::shared_ptr<BackgroundChannel>> rows;rows.reserve(info->channels.size());
        for (const auto channel:info->channels) {
            const auto *metadata=iiColorManager::channelInfo(channel);
            if (!metadata) continue;
            auto row=std::make_shared<BackgroundChannel>();row->channel=channel;row->key=std::string(metadata->name);
            row->label=std::string(metadata->label);row->minimum=metadata->minimum;row->maximum=metadata->maximum;row->step=metadata->step;
            row->owner=shared_from_this();(void)row->value.set(picker_.channelValue(channel));
            (void)row->text.set(number(row->value.get()));rows.push_back(std::move(row));
        }
        (void)channels.set(std::move(rows));
        (void)channelListHeight.set(std::max(1.0,rowHeight_*channels.get().size()));
    } else for (const auto &row:channels.get()) {
        (void)row->value.set(picker_.channelValue(row->channel));(void)row->text.set(number(row->value.get()));
    }
    if (allChannels.get().empty()) {
        using C=iiColorManager::Channel;
        constexpr std::array values{std::pair{C::Red,"RGB · R"},std::pair{C::Green,"RGB · G"},std::pair{C::Blue,"RGB · B"},
            std::pair{C::Hue,"HSB · H"},std::pair{C::Saturation,"HSB · S"},std::pair{C::Brightness,"HSB · B"},
            std::pair{C::Lightness,"Lab · L"},std::pair{C::LabA,"Lab · a"},std::pair{C::LabB,"Lab · b"},
            std::pair{C::Cyan,"CMYK · C"},std::pair{C::Magenta,"CMYK · M"},std::pair{C::Yellow,"CMYK · Y"},std::pair{C::Black,"CMYK · K"}};
        std::vector<std::shared_ptr<BackgroundChannel>> rows;rows.reserve(values.size());
        for (const auto &[channel,label]:values) {
            const auto *metadata=iiColorManager::channelInfo(channel);if (!metadata) continue;
            auto row=std::make_shared<BackgroundChannel>();row->channel=channel;row->key=std::string(metadata->name);row->label=label;
            row->minimum=metadata->minimum;row->maximum=metadata->maximum;row->step=metadata->step;row->owner=shared_from_this();
            (void)row->value.set(picker_.channelValue(channel));(void)row->text.set(number(row->value.get()));rows.push_back(std::move(row));
        }
        (void)allChannels.set(std::move(rows));
        (void)allChannelListHeight.set(std::max(1.0,rowHeight_*allChannels.get().size()));
    } else for (const auto &row:allChannels.get()) {
        (void)row->value.set(picker_.channelValue(row->channel));(void)row->text.set(number(row->value.get()));
    }
    requestSurface();
}
ii::core::Result<void> BackgroundPanelModel::selectMode(int index) {
    if (auto valid=writable();!valid) return valid;
    const auto modes=iiColorManager::modes();
    if (index<0 || static_cast<std::size_t>(index)>=modes.size()) return ii::core::fail(ErrorCode::out_of_range,"Unknown picker mode");
    if (hexPending_) {if (auto committed=commitHex();!committed) return committed;}
    (void)picker_.setMode(modes[static_cast<std::size_t>(index)].mode);(void)modeIndex.set(index);projectPicker(true);return {};
}
ii::core::Result<void> BackgroundPanelModel::selectFill(int index) {
    if (auto valid=writable();!valid) return valid;
    if (index<0 || index>2) return ii::core::fail(ErrorCode::out_of_range,"Unknown background fill");
    if (hexPending_) {if (auto committed=commitHex();!committed) return committed;}
    draft_.style=index==0?BackgroundFillStyle::Solid:index==1?BackgroundFillStyle::Linear:BackgroundFillStyle::Radial;
    (void)fillIndex.set(index);(void)gradient.set(index!=0);
    (void)picker_.editColor(index?draft_.stops[static_cast<std::size_t>(stopIndex.get())].color:draft_.color);
    projectPicker(false);return {};
}
ii::core::Result<void> BackgroundPanelModel::selectStop(int index) {
    if (auto valid=writable();!valid) return valid;
    if (!gradient.get() || index<0 || static_cast<std::size_t>(index)>=draft_.stops.size())
        return ii::core::fail(ErrorCode::out_of_range,"Unknown gradient stop");
    if (hexPending_) {if (auto committed=commitHex();!committed) return committed;}
    (void)stopIndex.set(index);(void)picker_.editColor(draft_.stops[static_cast<std::size_t>(index)].color);
    projectStops();projectPicker(false);return {};
}
ii::core::Result<void> BackgroundPanelModel::selectPreset(int index) {
    if (auto valid=writable();!valid) return valid;
    if (index==0) return {};
    if (index<0 || static_cast<std::size_t>(index)>presets_.size()) return ii::core::fail(ErrorCode::out_of_range,"Unknown gradient preset");
    if (hexPending_) {if (auto committed=commitHex();!committed) return committed;}
    const auto &colors=presets_[static_cast<std::size_t>(index-1)].colors;
    draft_.stops.clear();
    for (std::size_t i=0;i<colors.size();++i) draft_.stops.push_back({double(i)/double(colors.size()-1),colors[i]});
    (void)stopIndex.set(0);
    if (!gradient.get()) {draft_.style=BackgroundFillStyle::Linear;(void)fillIndex.set(1);(void)gradient.set(true);}
    (void)picker_.editColor(draft_.stops.front().color);projectStops();projectPicker(false);return {};
}
ii::core::Result<void> BackgroundPanelModel::selectSwatch(int index) {
    if (auto valid=writable();!valid) return valid;
    if (index==0) return {};
    if (index<0 || static_cast<std::size_t>(index)>swatches_.size()) return ii::core::fail(ErrorCode::out_of_range,"Unknown swatch");
    hexPending_=false;(void)picker_.editColor(swatches_[static_cast<std::size_t>(index-1)]);(void)status.set({});
    projectPicker(false);updateAvailability();return {};
}
ii::core::Result<void> BackgroundPanelModel::selectRecent(int index) {
    if (auto valid=writable();!valid) return valid;
    if (index==0) return {};
    if (index<0 || static_cast<std::size_t>(index)>recent_.size()) return ii::core::fail(ErrorCode::out_of_range,"Unknown recent color");
    hexPending_=false;(void)picker_.editColor(recent_[static_cast<std::size_t>(index-1)]);(void)status.set({});
    projectPicker(false);updateAvailability();return {};
}
ii::core::Result<void> BackgroundPanelModel::editHex(std::string value) {
    if (auto valid=writable();!valid) return valid;
    hexPending_=true;(void)hex.set(std::move(value));updateAvailability();return {};
}
ii::core::Result<void> BackgroundPanelModel::commitHex() {
    if (auto valid=writable();!valid) return valid;
    if (!picker_.editHex(hex.get())) {(void)status.set("Enter a valid hexadecimal color");return ii::core::fail(ErrorCode::invalid_argument,status.get());}
    hexPending_=false;(void)status.set({});projectPicker(false);updateAvailability();return {};
}
ii::core::Result<void> BackgroundPanelModel::setChannel(const BackgroundChannel *row,double value) {
    if (auto valid=writable();!valid) return valid;
    if (!row || (std::ranges::none_of(channels.get(),[&](const auto &item){return item.get()==row;}) &&
                 std::ranges::none_of(allChannels.get(),[&](const auto &item){return item.get()==row;})))
        return ii::core::fail(ErrorCode::closed,"Color channel is no longer active");
    if (!std::isfinite(value) || value<row->minimum || value>row->maximum)
        return ii::core::fail(ErrorCode::out_of_range,"Color channel is outside its range");
    if (hexPending_) {if (auto committed=commitHex();!committed) return committed;}
    if (!picker_.edit(row->channel,value)) return ii::core::fail(ErrorCode::invalid_argument,"Cannot edit color channel");
    (void)status.set({});projectPicker(false);return {};
}
ii::core::Result<void> BackgroundChannel::setValue(double value) {
    if (auto model=owner.lock()) return model->setChannel(this,value);
    return ii::core::fail(ErrorCode::closed,"Background panel is closed");
}
void BackgroundChannel::moved(double value) {(void)setValue(value);}
void BackgroundChannel::edited(std::string value) {(void)text.set(std::move(value));}
void BackgroundChannel::submitted() {
    const auto input=text.get();double parsed=0;
    const auto result=std::from_chars(input.data(),input.data()+input.size(),parsed);
    if (result.ec==std::errc{} && result.ptr==input.data()+input.size()) {
        if (auto changed=setValue(parsed);changed) return;
    }
    (void)text.set(number(value.get()));
}
ii::core::Result<void> BackgroundPanelModel::setSurface(bool rail,double x,double y) {
    if (auto valid=writable();!valid) return valid;
    if (!visible_ || !hasPlane.get() || !std::isfinite(x) || !std::isfinite(y))
        return ii::core::fail(ErrorCode::invalid_argument,"Color surface is unavailable");
    if (hexPending_) {if (auto committed=commitHex();!committed) return committed;}
    if (!picker_.editSurface(surface(rail),x,y)) return ii::core::fail(ErrorCode::invalid_argument,"Cannot edit color surface");
    projectPicker(false);return {};
}
iiColorManager::Surface BackgroundPanelModel::surface(bool rail) const {
    using iiColorManager::Mode;using iiColorManager::Surface;
    const auto mode=picker_.mode();
    if (rail) return mode==Mode::HueCube?Surface::HueRail:Surface::BrightnessRail;
    return mode==Mode::HueCube?Surface::HueCube:mode==Mode::BrightnessCube?Surface::BrightnessCube:Surface::ColorWheel;
}
iiColorManager::Point BackgroundPanelModel::surfacePosition(bool rail) const {return picker_.position(surface(rail));}
ii::core::Result<void> BackgroundPanelModel::setAlpha(double value) {
    if (auto valid=writable();!valid) return valid;
    if (!percent(value)) return ii::core::fail(ErrorCode::out_of_range,"Alpha must be between 0 and 100");
    draft_.alpha=value/100;(void)alpha.set(value);(void)alphaText.set(number(value)+"%");return {};
}
ii::core::Result<void> BackgroundPanelModel::setOpacity(double value) {
    if (auto valid=writable();!valid) return valid;
    if (!percent(value)) return ii::core::fail(ErrorCode::out_of_range,"Opacity must be between 0 and 100");
    draft_.opacity=value/100;(void)opacity.set(value);(void)opacityText.set(number(value)+"%");return {};
}
ii::core::Result<void> BackgroundPanelModel::setAngle(double value) {
    if (auto valid=writable();!valid) return valid;
    if (!std::isfinite(value) || value < -360 || value > 360) return ii::core::fail(ErrorCode::out_of_range,"Angle must be between -360 and 360 degrees");
    draft_.angleDegrees=value;(void)angle.set(value);(void)angleText.set(number(value)+"°");return {};
}
ii::core::Result<void> BackgroundPanelModel::setPerceptual(bool value) {
    if (auto valid=writable();!valid) return valid;
    draft_.perceptual=value;(void)perceptual.set(value);return {};
}
ii::core::Result<void> BackgroundPanelModel::setStopPosition(double value) {
    if (auto valid=writable();!valid) return valid;
    if (!gradient.get() || !percent(value)) return ii::core::fail(ErrorCode::out_of_range,"Gradient position must be between 0 and 100");
    draft_.stops[static_cast<std::size_t>(stopIndex.get())].position=value/100;projectStops();return {};
}
ii::core::Result<void> BackgroundPanelModel::addStop() {
    if (auto valid=writable();!valid) return valid;
    if (!gradient.get() || draft_.stops.size()>=32) return ii::core::fail(ErrorCode::out_of_range,"A gradient supports at most 32 stops");
    if (hexPending_) {if (auto committed=commitHex();!committed) return committed;}
    draft_.stops.push_back({.5,picker_.color()});(void)stopIndex.set(static_cast<int>(draft_.stops.size()-1));projectStops();return {};
}
ii::core::Result<void> BackgroundPanelModel::removeStop() {
    if (auto valid=writable();!valid) return valid;
    if (!gradient.get() || draft_.stops.size()<=2) return ii::core::fail(ErrorCode::out_of_range,"A gradient needs at least two stops");
    if (hexPending_) {if (auto committed=commitHex();!committed) return committed;}
    const auto index=static_cast<std::size_t>(stopIndex.get());draft_.stops.erase(draft_.stops.begin()+static_cast<std::ptrdiff_t>(index));
    (void)stopIndex.set(static_cast<int>(std::min(index,draft_.stops.size()-1)));
    (void)picker_.editColor(draft_.stops[static_cast<std::size_t>(stopIndex.get())].color);projectStops();projectPicker(false);return {};
}
void BackgroundPanelModel::requestSurface() {
    ++requestedSurface_;(void)surfaceVersion.set(requestedSurface_);
    if (!visible_ || !hasPlane.get() || replaceMode.get()) {(void)planeImage.set({});(void)railImage.set({});return;}
    scheduleSurface();
}
void BackgroundPanelModel::scheduleSurface() {
    if (!active_ || !visible_ || !hasPlane.get() || replaceMode.get() || surfaceInFlight_) return;
    const auto version=requestedSurface_;const auto picker=picker_;const auto plane=surface(false),rail=surface(true);auto bridge=bridge_;
    surfaceInFlight_=true;
    try {
        executor_->post([picker,plane,rail,version,bridge] {
            SurfaceResult result;result.version=version;
            try {
                auto a=iiColorManager::renderSurface(picker,plane,128,128),b=iiColorManager::renderSurface(picker,rail,16,128);
                if (!a || !b) result.error="Cannot render color picker";
                else {
                    auto image=ii::gui::rgba_image(a->width,a->height,std::move(a->rgba));
                    auto strip=ii::gui::rgba_image(b->width,b->height,std::move(b->rgba));
                    if (image && strip) {result.plane=std::move(*image);result.rail=std::move(*strip);}
                    else result.error="Cannot render color picker";
                }
            } catch (...) {result.error="Cannot render color picker";}
            {std::lock_guard lock(bridge->mutex);if (bridge->closed) return;bridge->surface=std::move(result);}
            (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
        });
    } catch (...) {surfaceInFlight_=false;(void)status.set("Color picker worker is unavailable");}
}
ii::core::Result<void> BackgroundPanelModel::apply() {
    if (auto valid=writable();!valid) return valid;
    if (!canApply.get()) return ii::core::fail(ErrorCode::unsupported,"The background draft is unavailable");
    auto source=document_->snapshot();const auto generation=document_->documentGeneration.get(),revision=document_->revision.get();
    const auto draft=draft_;submittedColor_=picker_.color();submittedImage_=false;auto bridge=bridge_;
    std::shared_ptr<std::atomic_bool> cancellation;
    try {cancellation=std::make_shared<std::atomic_bool>(false);}
    catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot schedule background operation",std::current_exception());}
    fillCancellation_=cancellation;
    (void)busy.set(true);updateAvailability();(void)status.set("Preparing background…");
    if (!active_ || !executor_ || cancellation->load(std::memory_order_relaxed)) {
        fillCancellation_.reset();(void)busy.set(false);updateAvailability();
        return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the background");
    }
    try {
        executor_->post([source=std::move(source),generation,revision,draft,bridge,cancellation] {
            auto result=prepareBackgroundFill(source,generation,revision,draft,[cancellation]{return cancellation->load(std::memory_order_relaxed);});
            {std::lock_guard lock(bridge->mutex);if (bridge->closed) return;bridge->fill.emplace(std::move(result));}
            (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
        });
    } catch (...) {
        fillCancellation_.reset();(void)busy.set(false);updateAvailability();(void)status.set("Background worker is unavailable");
        return ii::core::fail(ErrorCode::failed,status.get(),std::current_exception());
    }
    return {};
}
ii::core::Result<void> BackgroundPanelModel::applyImage() {
    if (auto valid=writable();!valid) return valid;
    if (!canApplyImage.get()) return ii::core::fail(ErrorCode::unsupported,"Select a background image first");
    auto source=document_->snapshot();const auto generation=document_->documentGeneration.get(),revision=document_->revision.get();
    const auto path=imagePath_;const auto fit=imageFit.get()==0?BackgroundImageFit::Cover:imageFit.get()==1?BackgroundImageFit::Contain:BackgroundImageFit::Stretch;
    auto bridge=bridge_;std::shared_ptr<std::atomic_bool> cancellation;
    try {cancellation=std::make_shared<std::atomic_bool>(false);}
    catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot schedule background operation",std::current_exception());}
    submittedImage_=true;fillCancellation_=cancellation;(void)busy.set(true);updateAvailability();(void)status.set("Preparing background…");
    if (!active_ || !executor_ || cancellation->load(std::memory_order_relaxed)) {
        fillCancellation_.reset();(void)busy.set(false);updateAvailability();
        return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the background");
    }
    try {
        executor_->post([source=std::move(source),generation,revision,path,fit,bridge,cancellation] {
            ii::core::Result<PreparedBackgroundFill> result=ii::core::fail(ErrorCode::failed,"Cannot read the background image");
            try {
                if (cancellation->load(std::memory_order_relaxed))
                    result=ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the background");
                else {
                    const bool image=native::isCanvasImage(path);
                    auto imported=image?native::importCanvasImage(path):native::openCanvasFile(path);
                    if (!imported) result=std::unexpected(imported.error());
                    else if (cancellation->load(std::memory_order_relaxed)) result=ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the background");
                    else if (image) {
                        const auto *asset=iiSharedCanvas::findRasterAsset(imported->document,"image");
                        if (!asset) result=ii::core::fail(ErrorCode::failed,"Image decoder returned no pixels");
                        else result=prepareBackgroundImage(source,generation,revision,asset->pixels,fit,
                                                           [cancellation]{return cancellation->load(std::memory_order_relaxed);});
                    } else {
                        const auto extent=iiSharedCanvas::documentViewRegion(imported->document).extent;
                        const iiSharedCanvas::MediaLimits limits;
                        const auto count=extent.width>0 && extent.height>0?
                            std::uint64_t(extent.width)*std::uint64_t(extent.height):0;
                        if (!count || count>limits.maxPixelsPerFrame || count>limits.maxDecodedBytes/4)
                            result=ii::core::fail(ErrorCode::out_of_range,"Canvas source exceeds the background image limit");
                        else {
                            auto rendered=iiSharedCanvas::renderFrame(imported->document,0);
                            if (!rendered.ok()) result=ii::core::fail(ErrorCode::failed,
                                rendered.message.empty()?"Cannot render the canvas source":rendered.message);
                            else if (cancellation->load(std::memory_order_relaxed))
                                result=ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the background");
                            else if (iiFileProvider::ObjectSource::inspect(path)!=imported->version.identity)
                                result=ii::core::fail(ErrorCode::busy,"Canvas source changed while it was being rendered");
                            else result=prepareBackgroundImage(source,generation,revision,rendered.pixels,fit,
                                                               [cancellation]{return cancellation->load(std::memory_order_relaxed);});
                        }
                    }
                }
            } catch (...) {result=ii::core::fail(ErrorCode::failed,"Cannot prepare the background image",std::current_exception());}
            {std::lock_guard lock(bridge->mutex);if (bridge->closed) return;bridge->fill.emplace(std::move(result));}
            (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
        });
    } catch (...) {
        fillCancellation_.reset();(void)busy.set(false);updateAvailability();(void)status.set("Background worker is unavailable");
        return ii::core::fail(ErrorCode::failed,status.get(),std::current_exception());
    }
    return {};
}
void BackgroundPanelModel::deliver() {
    if (!active_) return;
    std::optional<SurfaceResult> surfaceResult;
    std::optional<ii::core::Result<PreparedBackgroundFill>> fillResult;
    {std::lock_guard lock(bridge_->mutex);surfaceResult=std::move(bridge_->surface);bridge_->surface.reset();
     fillResult=std::move(bridge_->fill);bridge_->fill.reset();}
    if (surfaceResult) {
        surfaceInFlight_=false;
        if (visible_ && hasPlane.get() && !replaceMode.get() && surfaceResult->version==requestedSurface_) {
            (void)planeImage.set(std::move(surfaceResult->plane));(void)railImage.set(std::move(surfaceResult->rail));
            if (!surfaceResult->error.empty()) (void)status.set(std::move(surfaceResult->error));
        } else scheduleSurface();
    }
    if (fillResult) {
        fillCancellation_.reset();
        if (*fillResult) {
            auto committed=document_->commitBackgroundFill(std::move(**fillResult));
            (void)status.set(committed?"Background applied":committed.error().message);
            if (committed && !submittedImage_) {
                const auto text=colorText(submittedColor_);
                std::erase_if(recent_,[&](const auto color){return colorText(color)==text;});
                recent_.insert(recent_.begin(),submittedColor_);
                if (recent_.size()>16) recent_.resize(16);
                std::vector<std::string> names{"Recent colors"};
                for (const auto color:recent_) names.push_back(colorText(color));
                (void)recentNames.set(std::move(names));
            }
        } else (void)status.set(fillResult->error().message);
        (void)busy.set(false);updateAvailability();
    }
}
void BackgroundPanelModel::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Background panel requires its owner thread");
    if (!active_) return;
    active_=false;visible_=false;++requestedSurface_;documentObserver_={};revisionObserver_={};showMask_={};pickImage_={};
    if (fillCancellation_) fillCancellation_->store(true,std::memory_order_relaxed);
    fillCancellation_.reset();
    if (bridge_) {std::lock_guard lock(bridge_->mutex);bridge_->closed=true;bridge_->surface.reset();bridge_->fill.reset();}
    (void)sender_.cancel(wake_);executor_.reset();(void)canApply.set(false);(void)canChooseImage.set(false);(void)canApplyImage.set(false);(void)busy.set(false);
    (void)planeImage.set({});(void)railImage.set({});
}
}
