#include "NativeElementsPanel.h"
#include "NativeVectorGeometry.h"
#include "NativeAssets.h"
#include <iiColorManager>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace dreamscapes::editor {
namespace {
using ii::core::ErrorCode;
using namespace iiSharedCanvas;
struct FillState {std::uint32_t argb;bool mixed;};
std::optional<FillState> solidFill(const Document *document,std::string_view id) {
    if (!document) return std::nullopt;
    const auto *layer=findLayer(*document,std::string(id));
    const auto *vector=layer?std::get_if<StaticVectorLayer>(layer):nullptr;
    if (!vector || !vector->properties.motion.empty()) return std::nullopt;
    const auto *asset=findVectorAsset(*document,vector->content.assetId);
    if (!asset || asset->paths.empty() || std::ranges::any_of(asset->paths,[](const auto &path){return !path.fill;}))
        return std::nullopt;
    const auto color=asset->paths.front().fill->argb;
    return FillState{color,std::ranges::any_of(asset->paths,[&](const auto &path){return path.fill->argb!=color;})};
}
std::string fillHex(std::uint32_t argb) {
    char text[10];
    const auto red=unsigned((argb>>16)&255),green=unsigned((argb>>8)&255),blue=unsigned(argb&255),alpha=unsigned(argb>>24);
    if (alpha==255) std::snprintf(text,sizeof(text),"#%02X%02X%02X",red,green,blue);
    else std::snprintf(text,sizeof(text),"#%02X%02X%02X%02X",red,green,blue,alpha);
    return text;
}
std::string format(double value) {
    char text[64];const auto result=std::to_chars(text,text+sizeof(text),value,std::chars_format::general,10);
    return result.ec==std::errc{}?std::string(text,result.ptr):std::string{};
}
std::optional<double> number(std::string_view text) {
    const auto first=text.find_first_not_of(" \t\r\n"),last=text.find_last_not_of(" \t\r\n");
    if (first==std::string_view::npos) return std::nullopt;
    text=text.substr(first,last-first+1);double value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if (parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value) || value<1 || value>65536)
        return std::nullopt;
    return value;
}
}
ElementsPanelModel::ElementsPanelModel(std::shared_ptr<native::DocumentSession> document):document_(std::move(document)) {
    if (!document_) throw std::invalid_argument("Elements panel requires a document session");
}
std::shared_ptr<ElementsPanelModel> ElementsPanelModel::create(std::shared_ptr<native::DocumentSession> document) {
    auto value=std::shared_ptr<ElementsPanelModel>(new ElementsPanelModel(std::move(document)));value->start();return value;
}
void ElementsPanelModel::start() {
    static const auto defaults=parseShapeDefaults(native::toolCatalog);
    if (!defaults) throw std::runtime_error(defaults.error().message);
    shapeTool_=ShapeToolModel::create(document_,*defaults);
    const std::weak_ptr<ElementsPanelModel> weak=shared_from_this();
    auto update=[weak](const auto &,std::uint64_t){if (auto self=weak.lock();self && self->visible_) self->synchronize();};
    selection_=document_->selectedLayer.observe(update);revision_=document_->revision.observe(update);
    generation_=document_->documentGeneration.observe(update);
}
void ElementsPanelModel::setVisible(bool value) {
    if (owner_!=std::this_thread::get_id() || visible_==value) return;
    visible_=value;
    if (shapeTool_) shapeTool_->setVisible(value && !shapeTool_->selecting.get());
    if (visible_) synchronize();
    else {
        currentId_.clear();edited_=Edited::none;fillEdited_=false;if (synchronizing_) pending_=true;
        (void)editable.set(false);if (!visible_) (void)fillEditable.set(false);
    }
}
ii::core::Result<void> ElementsPanelModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Elements panel requires its owner thread");
    return {};
}
void ElementsPanelModel::synchronize() {
    if (owner_!=std::this_thread::get_id() || !visible_) return;
    if (synchronizing_) {pending_=true;return;}
    synchronizing_=true;
    try {
        do {
            pending_=false;
            if (!visible_) break;
            const auto id=document_->hasCurrentLayerSelection()?document_->selectedLayer.get():std::string{};
            const auto gen=document_->documentGeneration.get();
            const auto snapshot=document_->snapshot();const auto *doc=snapshot.get();const auto *layer=doc?findLayer(*doc,id):nullptr;
            const auto value=vectorGeometry(doc,id);
            const auto fill=value?solidFill(doc,id):std::nullopt;
            const auto name=layer?layerProperties(*layer).name:std::string{};
            currentId_=value?id:std::string{};currentGeneration_=gen;edited_=Edited::none;fillEdited_=false;
            (void)editable.set(bool(value));if (pending_) continue;
            (void)fillEditable.set(bool(fill));if (pending_) continue;
            (void)selectedName.set(name);if (pending_) continue;
            (void)elementWidth.set(value?value->width:0);if (pending_) continue;
            (void)elementHeight.set(value?value->height:0);if (pending_) continue;
            (void)widthText.set(value?format(value->width):std::string{});
            if (pending_) continue;
            (void)heightText.set(value?format(value->height):std::string{});
            if (pending_) continue;
            (void)fillText.set(fill?(fill->mixed?std::string("Mixed"):fillHex(fill->argb)):std::string{});
            if (pending_) continue;
            (void)status.set(value?std::string{}:"Select a resizable static vector layer without motion");
        } while (pending_);
        synchronizing_=false;
    } catch (...) {synchronizing_=false;throw;}
}
ii::core::Result<void> ElementsPanelModel::editWidth(std::string value) {
    if (auto valid=writable();!valid) return valid;
    if (!editable.get()) return ii::core::fail(ErrorCode::closed,"Select an editable vector layer");
    if (value.size()>64) return ii::core::fail(ErrorCode::invalid_argument,"Width input is too long");
    edited_=Edited::width;(void)widthText.set(std::move(value));return {};
}
ii::core::Result<void> ElementsPanelModel::editHeight(std::string value) {
    if (auto valid=writable();!valid) return valid;
    if (!editable.get()) return ii::core::fail(ErrorCode::closed,"Select an editable vector layer");
    if (value.size()>64) return ii::core::fail(ErrorCode::invalid_argument,"Height input is too long");
    edited_=Edited::height;(void)heightText.set(std::move(value));return {};
}
ii::core::Result<void> ElementsPanelModel::setConstrained(bool value) {
    if (auto valid=writable();!valid) return valid;
    (void)constrained.set(value);return {};
}
ii::core::Result<void> ElementsPanelModel::editFill(std::string value) {
    if (auto valid=writable();!valid) return valid;
    if (!fillEditable.get()) return ii::core::fail(ErrorCode::closed,"Select a vector with solid fills");
    if (value.size()>64) return ii::core::fail(ErrorCode::invalid_argument,"Fill input is too long");
    fillEdited_=true;(void)fillText.set(std::move(value));return {};
}
ii::core::Result<void> ElementsPanelModel::applyFill() {
    if (auto valid=writable();!valid) return valid;
    if (!visible_ || !fillEditable.get() || !document_->hasCurrentLayerSelection() || currentId_!=document_->selectedLayer.get() ||
        currentGeneration_!=document_->documentGeneration.get())
        return ii::core::fail(ErrorCode::closed,"Select a vector with solid fills");
    if (!fillEdited_) return {};
    const auto input=fillText.get();const auto color=iiColorManager::parseHexValue(input);
    if (!color) {
        (void)status.set("Enter an RGB or RGBA hexadecimal color");
        return ii::core::fail(ErrorCode::invalid_argument,status.get());
    }
    const auto first=input.find_first_not_of(" \t\r\n"),last=input.find_last_not_of(" \t\r\n");
    auto digits=std::string_view(input).substr(first,last-first+1);
    if (digits.starts_with('#')) digits.remove_prefix(1);
    const bool hasAlpha=digits.size()==4 || digits.size()==8;
    const auto byte=[](double value){return static_cast<std::uint32_t>(std::lround(value*255));};
    const auto rgb=(byte(color->r)<<16)|(byte(color->g)<<8)|byte(color->b);
    const auto alpha=hasAlpha?std::optional<std::uint8_t>(static_cast<std::uint8_t>(byte(color->a))):std::nullopt;
    auto result=document_->setVectorSolidFill(currentId_,rgb,alpha);
    if (!result) {(void)status.set(result.error().message);return result;}
    synchronize();return {};
}
ii::core::Result<void> ElementsPanelModel::apply() {
    if (auto valid=writable();!valid) return valid;
    if (!editable.get() || !document_->hasCurrentLayerSelection() || currentId_!=document_->selectedLayer.get() ||
        currentGeneration_!=document_->documentGeneration.get())
        return ii::core::fail(ErrorCode::closed,"Select an editable vector layer");
    const auto widthValue=number(widthText.get()),heightValue=number(heightText.get());
    const auto current=vectorGeometry(document_->document(),currentId_);
    if (!widthValue || !heightValue || !current) {
        synchronize();(void)status.set("Enter dimensions between 1 and 65536 pixels");
        return ii::core::fail(ErrorCode::invalid_argument,status.get());
    }
    double w=*widthValue,h=*heightValue;
    if (constrained.get()) {
        if (edited_==Edited::width) h=current->height*w/current->width;
        else if (edited_==Edited::height) w=current->width*h/current->height;
    }
    if (!std::isfinite(w)||!std::isfinite(h)||w<1||h<1||w>65536||h>65536) {
        synchronize();(void)status.set("Constrained dimensions exceed the supported range");
        return ii::core::fail(ErrorCode::out_of_range,status.get());
    }
    auto next=current->transform;const auto sx=w/current->width,sy=h/current->height;
    next.m11*=sx;next.m12*=sx;next.m21*=sy;next.m22*=sy;
    next.translationX+=(current->transform.m11-next.m11)*current->box.left+(current->transform.m21-next.m21)*current->box.top;
    next.translationY+=(current->transform.m12-next.m12)*current->box.left+(current->transform.m22-next.m22)*current->box.top;
    auto result=document_->setLayerTransform(currentId_,next);
    if (!result) {(void)status.set(result.error().message);return result;}
    synchronize();return {};
}
void ElementsPanelModel::sizeChanged(double w,double h,double inset,double gap) {
    if (!std::isfinite(w)||!std::isfinite(h)||!std::isfinite(inset)||!std::isfinite(gap)||w<0||h<0||w>8192||h>8192||inset<0||gap<0) return;
    const auto content=std::max(1.0,w-inset);
    (void)width.set(std::max(1.0,w));(void)height.set(std::max(1.0,h));(void)contentWidth.set(content);
    (void)fieldWidth.set(std::max(1.0,(content-gap)/2));
}
}
