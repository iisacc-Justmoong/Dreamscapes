#include "NativeCanvasPanel.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dreamscapes::editor {
namespace {
using ii::core::ErrorCode;
std::optional<std::int32_t> dimension(std::string_view value) {
    auto trim=[](std::string_view &v) {
        const auto first=v.find_first_not_of(" \t\r\n"),last=v.find_last_not_of(" \t\r\n");
        v=first==std::string_view::npos?std::string_view{}:v.substr(first,last-first+1);
    };
    trim(value);
    if (value.ends_with("px")) {value.remove_suffix(2);trim(value);}
    if (value.empty()) return {};
    std::int32_t result=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if (parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || result<1 || result>65536) return {};
    return result;
}
std::optional<double> rotationAngle(std::string_view value) {
    auto trim=[](std::string_view &v) {
        const auto first=v.find_first_not_of(" \t\r\n"),last=v.find_last_not_of(" \t\r\n");
        v=first==std::string_view::npos?std::string_view{}:v.substr(first,last-first+1);
    };
    trim(value);
    if (value.ends_with("°")) {value.remove_suffix(std::string_view("°").size());trim(value);}
    if (value.starts_with('+')) value.remove_prefix(1);
    if (value.empty()) return {};
    double result=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if (parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() ||
        !std::isfinite(result) || result< -180 || result>180) return {};
    return result;
}
std::string formatAngle(double value) {
    char buffer[64];
    const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value);
    return result.ec==std::errc{}?std::string(buffer,result.ptr):std::string("0");
}
}
CanvasPanelModel::CanvasPanelModel(std::shared_ptr<native::DocumentSession> document) : document_(std::move(document)) {
    if (!document_) throw std::invalid_argument("Canvas panel requires a document session");
}
std::shared_ptr<CanvasPanelModel> CanvasPanelModel::create(std::shared_ptr<native::DocumentSession> document) {
    auto value=std::shared_ptr<CanvasPanelModel>(new CanvasPanelModel(std::move(document)));value->start();return value;
}
void CanvasPanelModel::start() {
    const std::weak_ptr<CanvasPanelModel> weak=shared_from_this();
    auto changed=[weak](const auto &,std::uint64_t){if (auto value=weak.lock()) value->synchronize();};
    revisionObserver_=document_->revision.observe(changed);generationObserver_=document_->documentGeneration.observe(changed);
}
void CanvasPanelModel::setVisible(bool value) {
    if (owner_!=std::this_thread::get_id() || visible_==value) return;
    visible_=value;if (visible_) synchronize();
}
void CanvasPanelModel::synchronize() {
    if (!visible_ || owner_!=std::this_thread::get_id()) return;
    if (synchronizing_) {pending_=true;return;}
    synchronizing_=true;
    try {
        do {
            pending_=false;
            const auto snapshot=document_->snapshot();const auto generation=document_->documentGeneration.get();
            const auto w=snapshot?snapshot->extent.width:0,h=snapshot?snapshot->extent.height:0;
            const bool replaced=generation!=generation_ || w!=acceptedWidth_ || h!=acceptedHeight_;
            if (replaced) editing_=false;
            generation_=generation;acceptedWidth_=w;acceptedHeight_=h;
            const auto reason=document_->canvasTransformStatus();
            (void)editable.set(snapshot && reason.empty());
            if (!reason.empty()) (void)status.set(reason);
            else if (!editing_) (void)status.set({});
            if (!editing_) {
                (void)widthText.set(snapshot?std::to_string(w):std::string{});
                (void)heightText.set(snapshot?std::to_string(h):std::string{});
            }
        } while (pending_);
        synchronizing_=false;
    } catch (...) {synchronizing_=false;throw;}
}
ii::core::Result<void> CanvasPanelModel::edit(ii::core::Observable<std::string> &field,std::string input) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Canvas panel requires its owner thread");
    if (!visible_ || !editable.get()) return ii::core::fail(ErrorCode::closed,"Canvas size is unavailable");
    if (input.size()>64) return ii::core::fail(ErrorCode::out_of_range,"Canvas dimension input is too long");
    editing_=true;(void)field.set(std::move(input));(void)status.set({});return {};
}
ii::core::Result<void> CanvasPanelModel::editWidth(std::string value) {return edit(widthText,std::move(value));}
ii::core::Result<void> CanvasPanelModel::editHeight(std::string value) {return edit(heightText,std::move(value));}
ii::core::Result<void> CanvasPanelModel::apply() {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Canvas panel requires its owner thread");
    if (!visible_ || !editable.get()) return ii::core::fail(ErrorCode::closed,"Canvas size is unavailable");
    const auto w=dimension(widthText.get()),h=dimension(heightText.get());
    if (!w || !h) {
        (void)status.set("Enter dimensions from 1 to 65536 pixels");
        return ii::core::fail(ErrorCode::invalid_argument,status.get());
    }
    editing_=false;
    auto result=document_->resizeCanvasCentered(*w,*h);
    if (!result) {editing_=true;(void)status.set(result.error().message);return result;}
    synchronize();return {};
}
ii::core::Result<void> CanvasPanelModel::reset() {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Canvas panel requires its owner thread");
    if (!visible_ || !document_->document()) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (mode_==Mode::mirror) return chooseHorizontal();
    if (mode_==Mode::rotate) {
        if (auto selected=chooseLeft(); !selected) return selected;
        return moveAngle(0);
    }
    editing_=false;synchronize();return {};
}
ii::core::Result<void> CanvasPanelModel::selectMode(Mode mode) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Canvas panel requires its owner thread");
    mode_=mode;
    (void)sizeMode.set(mode==Mode::size);
    if (mode_!=mode) return {};
    (void)rotateMode.set(mode==Mode::rotate);
    if (mode_!=mode) return {};
    (void)mirrorMode.set(mode==Mode::mirror);return {};
}
ii::core::Result<void> CanvasPanelModel::showSize() {return selectMode(Mode::size);}
ii::core::Result<void> CanvasPanelModel::showRotate() {return selectMode(Mode::rotate);}
ii::core::Result<void> CanvasPanelModel::showMirror() {return selectMode(Mode::mirror);}
ii::core::Result<void> CanvasPanelModel::selectPreset(int value) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Canvas panel requires its owner thread");
    preset_=value;
    (void)left.set(value==-90);
    if (preset_!=value) return {};
    (void)half.set(value==180);
    if (preset_!=value) return {};
    (void)right.set(value==90);return {};
}
ii::core::Result<void> CanvasPanelModel::chooseLeft() {return selectPreset(-90);}
ii::core::Result<void> CanvasPanelModel::chooseHalf() {return selectPreset(180);}
ii::core::Result<void> CanvasPanelModel::chooseRight() {return selectPreset(90);}
ii::core::Result<void> CanvasPanelModel::editAngle(std::string value) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Canvas panel requires its owner thread");
    if (!visible_ || !editable.get() || mode_!=Mode::rotate) return ii::core::fail(ErrorCode::closed,"Canvas rotation is unavailable");
    if (value.size()>64) return ii::core::fail(ErrorCode::out_of_range,"Rotation input is too long");
    (void)angleText.set(std::move(value));(void)status.set({});return {};
}
ii::core::Result<void> CanvasPanelModel::commitAngle() {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Canvas panel requires its owner thread");
    if (!visible_ || !editable.get() || mode_!=Mode::rotate) return ii::core::fail(ErrorCode::closed,"Canvas rotation is unavailable");
    const auto value=rotationAngle(angleText.get());
    if (!value) {
        (void)status.set("Enter a custom angle from -180 to 180 degrees");
        return ii::core::fail(ErrorCode::invalid_argument,status.get());
    }
    return moveAngle(*value);
}
ii::core::Result<void> CanvasPanelModel::moveAngle(double value) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Canvas panel requires its owner thread");
    if (!visible_ || !editable.get() || mode_!=Mode::rotate) return ii::core::fail(ErrorCode::closed,"Canvas rotation is unavailable");
    if (!std::isfinite(value) || value< -180 || value>180) return ii::core::fail(ErrorCode::out_of_range,"Rotation is outside the supported range");
    (void)angle.set(value);(void)angleText.set(formatAngle(value));(void)status.set({});return {};
}
ii::core::Result<void> CanvasPanelModel::applyRotate() {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Canvas panel requires its owner thread");
    if (!visible_ || !editable.get() || mode_!=Mode::rotate) return ii::core::fail(ErrorCode::closed,"Canvas rotation is unavailable");
    const auto generation=document_->documentGeneration.get();
    if (auto committed=commitAngle(); !committed) return committed;
    if (generation!=document_->documentGeneration.get() || !visible_ || !editable.get() || mode_!=Mode::rotate)
        return ii::core::fail(ErrorCode::cancelled,"The active canvas or tool changed during rotation");
    auto result=document_->rotateCanvas(double(preset_)+angle.get());
    if (!result) {(void)status.set(result.error().message);return result;}
    (void)status.set({});return {};
}
ii::core::Result<void> CanvasPanelModel::selectAxis(bool value) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Canvas panel requires its owner thread");
    horizontalSelected_=value;
    (void)horizontal.set(value);
    if (horizontalSelected_!=value) return {};
    (void)vertical.set(!value);return {};
}
ii::core::Result<void> CanvasPanelModel::chooseHorizontal() {return selectAxis(true);}
ii::core::Result<void> CanvasPanelModel::chooseVertical() {return selectAxis(false);}
ii::core::Result<void> CanvasPanelModel::applyMirror() {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Canvas panel requires its owner thread");
    if (!visible_ || !editable.get() || mode_!=Mode::mirror) return ii::core::fail(ErrorCode::closed,"Canvas mirror is unavailable");
    auto result=document_->mirrorCanvas(horizontalSelected_);
    if (!result) {(void)status.set(result.error().message);return result;}
    (void)status.set({});return {};
}
void CanvasPanelModel::sizeChanged(double w,double h,double inset) {
    if (!std::isfinite(w)||!std::isfinite(h)||!std::isfinite(inset)||w<0||h<0||w>8192||h>8192||inset<0) return;
    const auto content=std::max(lvui::SliderConfiguration{}.range_inset*2,w-inset);
    (void)width.set(std::max(1.0,w));(void)height.set(std::max(1.0,h));(void)contentWidth.set(content);
    (void)fieldWidth.set(std::max(1.0,(content-inset/2)/2));
}
}
