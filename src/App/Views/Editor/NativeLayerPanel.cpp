#include "NativeLayerPanel.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace dreamscapes::editor {
namespace {
using ii::core::ErrorCode;
struct BlendEntry {std::string_view name; RasterBlendMode value;};
constexpr std::array blendTable{
    BlendEntry{"Normal",RasterBlendMode::SourceOver},
    BlendEntry{"Multiply",RasterBlendMode::Multiply}, BlendEntry{"Screen",RasterBlendMode::Screen},
    BlendEntry{"Overlay",RasterBlendMode::Overlay}, BlendEntry{"Darken",RasterBlendMode::Darken},
    BlendEntry{"Lighten",RasterBlendMode::Lighten}, BlendEntry{"Color Burn",RasterBlendMode::ColorBurn},
    BlendEntry{"Linear Burn",RasterBlendMode::LinearBurn}, BlendEntry{"Darker Color",RasterBlendMode::DarkerColor},
    BlendEntry{"Color Dodge",RasterBlendMode::ColorDodge}, BlendEntry{"Linear Dodge",RasterBlendMode::LinearDodge},
    BlendEntry{"Lighter Color",RasterBlendMode::LighterColor}, BlendEntry{"Soft Light",RasterBlendMode::SoftLight},
    BlendEntry{"Hard Light",RasterBlendMode::HardLight}, BlendEntry{"Vivid Light",RasterBlendMode::VividLight},
    BlendEntry{"Linear Light",RasterBlendMode::LinearLight}, BlendEntry{"Pin Light",RasterBlendMode::PinLight},
    BlendEntry{"Hard Mix",RasterBlendMode::HardMix}, BlendEntry{"Difference",RasterBlendMode::Difference},
    BlendEntry{"Exclusion",RasterBlendMode::Exclusion}, BlendEntry{"Subtract",RasterBlendMode::Subtract},
    BlendEntry{"Divide",RasterBlendMode::Divide}, BlendEntry{"Hue",RasterBlendMode::Hue},
    BlendEntry{"Saturation",RasterBlendMode::Saturation}, BlendEntry{"Color",RasterBlendMode::Color},
    BlendEntry{"Luminosity",RasterBlendMode::Luminosity}, BlendEntry{"Dissolve",RasterBlendMode::Dissolve}};
std::string formatPercent(double value) {
    if (value==0) value=0;
    char buffer[48];
    auto result=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,6);
    return result.ec==std::errc{}?std::string(buffer,result.ptr)+"%":std::string{};
}
std::string typeLabel(const iiSharedCanvas::Layer &layer) {
    using namespace iiSharedCanvas;
    auto kind=layerKind(layer);
    if (kind) switch (*kind) {
    case LayerKind::StaticBitmap:return "Static bitmap";
    case LayerKind::StaticVector:return "Static vector";
    case LayerKind::DynamicBitmap:return "Dynamic bitmap";
    case LayerKind::DynamicVector:return "Dynamic vector";
    }
    if (contentKind(layer)==ContentKind::Video) return "Video";
    if (contentKind(layer)==ContentKind::IpAdapter) return "Embedding";
    return "ControlNet";
}
std::vector<LayerInformationRow> informationFor(const iiSharedCanvas::Document *document,
                                                const iiSharedCanvas::Layer *layer,
                                                iiSharedCanvas::FrameIndex frame) {
    using namespace iiSharedCanvas;
    std::string dimensions="Unavailable",depth="Unavailable",source="Unavailable",origin="Unavailable";
    const auto *asset=document && layer?resolveAssetAt(*document,*layer,frame):nullptr;
    if (asset) {
        origin="Embedded media";
        std::visit([&](const auto &value) {
            using T=std::decay_t<decltype(value)>;
            source=value.id;
            auto extent=[&](auto width,auto height) {
                if (width>0 && height>0) dimensions=std::to_string(width)+" × "+std::to_string(height)+" px";
            };
            if constexpr (std::is_same_v<T,RasterAsset>) {
                extent(value.pixels.width,value.pixels.height);
                depth="8 bit / channel";
            } else if constexpr (std::is_same_v<T,VideoAsset>) {
                if (!value.frames.empty()) {
                    extent(value.frames.front().width,value.frames.front().height);
                    depth="8 bit / channel";
                }
            } else if constexpr (requires {value.viewport;}) {
                extent(value.viewport.width,value.viewport.height);
            }
        },*asset);
    }
    return {{"dimensions","Dimensions",std::move(dimensions)},
            {"profile","Color profile","Unavailable"},
            {"bitDepth","Bit depth",std::move(depth)},
            {"type","Layer type",layer?typeLabel(*layer):"Unavailable"},
            {"origin","Origin",std::move(origin)},
            {"source","Source",std::move(source)}};
}
}
LayerPanelModel::LayerPanelModel(std::shared_ptr<native::DocumentSession> value) : document_(std::move(value)) {
    if (!document_) throw std::invalid_argument("Layer panel requires a document session");
}
std::shared_ptr<LayerPanelModel> LayerPanelModel::create(std::shared_ptr<native::DocumentSession> document) {
    auto value=std::shared_ptr<LayerPanelModel>(new LayerPanelModel(std::move(document))); value->start(); return value;
}
void LayerPanelModel::start() {
    std::vector<LayerBlendOption> values; values.reserve(blendTable.size());
    for (const auto &entry:blendTable) values.push_back({std::string(entry.name),std::string(entry.name)});
    (void)blends.set(std::move(values));
    const std::weak_ptr<LayerPanelModel> weak=shared_from_this();
    auto update=[weak](const auto &,std::uint64_t){if (auto self=weak.lock()) self->synchronize();};
    selectionObserver_=document_->selectedLayer.observe(update); revisionObserver_=document_->revision.observe(update);
    generationObserver_=document_->documentGeneration.observe(update);
    frameObserver_=document_->frame.observe([weak](const auto &,std::uint64_t) {
        if (auto self=weak.lock(); self && self->informationMode.get()) self->synchronize();
    });
    synchronize();
}
ii::core::Result<void> LayerPanelModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Layer inspector requires its owner thread");
    return {};
}
void LayerPanelModel::synchronize() {
    if (owner_!=std::this_thread::get_id()) return;
    if (synchronizing_) {pending_=true; return;}
    synchronizing_=true;
    try {
        do {
            pending_=false;
            const auto id=document_->hasCurrentLayerSelection()?document_->selectedLayer.get():std::string{};
            const auto *doc=document_->document();
            const auto *layer=doc?iiSharedCanvas::findLayer(*doc,id):nullptr;
            std::string name,label,blendName; double percent=0;
            auto rows=informationMode.get()?informationFor(doc,layer,static_cast<iiSharedCanvas::FrameIndex>(document_->frame.get()))
                                           :std::vector<LayerInformationRow>{};
            if (layer) {
                const auto &properties=iiSharedCanvas::layerProperties(*layer);
                name=properties.name; label=name+" · "+typeLabel(*layer); percent=properties.opacity*100;
                auto match=std::ranges::find_if(blendTable,[&](const auto &entry){return entry.value==properties.blendMode;});
                blendName=match==blendTable.end()?"Unavailable":std::string(match->name);
            }
            const bool duplicable=layer && iiSharedCanvas::layerKind(*layer).has_value();
            const bool reset=!layer || currentId_!=id || !editing_ || acceptedOpacity_!=percent;
            if (currentId_!=id || !layer || acceptedOpacity_!=percent) editing_=false;
            currentId_=layer?id:std::string{}; acceptedOpacity_=percent;
            (void)hasSelection.set(bool(layer)); (void)canDuplicate.set(duplicable);
            (void)selectedName.set(std::move(name));
            (void)selectedLabel.set(std::move(label)); (void)opacity.set(percent); (void)blend.set(std::move(blendName));
            if (reset) (void)opacityText.set(layer?formatPercent(percent):std::string{});
            if (informationMode.get()) (void)information.set(std::move(rows));
        } while (pending_);
        synchronizing_=false;
    } catch (...) {synchronizing_=false; throw;}
}
ii::core::Result<void> LayerPanelModel::showLayer() {
    if (auto valid=writable(); !valid) return valid;
    editing_=false;
    (void)informationMode.set(false); (void)layerMode.set(true); (void)viewTitle.set("Layer");
    synchronize(); return {};
}
ii::core::Result<void> LayerPanelModel::showInformation() {
    if (auto valid=writable(); !valid) return valid;
    editing_=false;
    (void)layerMode.set(false); (void)informationMode.set(true); (void)viewTitle.set("Information");
    synchronize(); return {};
}
ii::core::Result<void> LayerPanelModel::editOpacityText(std::string value) {
    if (auto valid=writable(); !valid) return valid;
    if (!hasSelection.get()) return ii::core::fail(ErrorCode::closed,"Select a layer first");
    if (value.size()>64) return ii::core::fail(ErrorCode::invalid_argument,"Opacity input is too long");
    editing_=true; (void)opacityText.set(std::move(value)); (void)status.set({}); return {};
}
ii::core::Result<void> LayerPanelModel::commitOpacity() {
    if (auto valid=writable(); !valid) return valid;
    if (!hasSelection.get()) return ii::core::fail(ErrorCode::closed,"Select a layer first");
    auto value=opacityText.get();
    auto trim=[](std::string &input) {
        const auto first=input.find_first_not_of(" \t\n\r"),last=input.find_last_not_of(" \t\n\r");
        input=first==std::string::npos?std::string{}:input.substr(first,last-first+1);
    };
    trim(value); if (!value.empty() && value.back()=='%') {value.pop_back();trim(value);}
    double number=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
    if (value.empty() || parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() ||
        !std::isfinite(number) || number<0 || number>100) {
        editing_=false; synchronize(); (void)status.set("Enter opacity between 0 and 100 percent");
        return ii::core::fail(ErrorCode::invalid_argument,status.get());
    }
    editing_=false; return moveOpacity(number);
}
ii::core::Result<void> LayerPanelModel::moveOpacity(double value) {
    if (auto valid=writable(); !valid) return valid;
    if (!hasSelection.get()) return ii::core::fail(ErrorCode::closed,"Select a layer first");
    if (!std::isfinite(value) || value<0 || value>100)
        return ii::core::fail(ErrorCode::invalid_argument,"Opacity must be between 0 and 100 percent");
    const auto id=currentId_; editing_=false;
    auto result=document_->setLayerOpacity(id,value/100.0);
    if (!result) {(void)status.set(result.error().message);synchronize();return result;}
    synchronize(); (void)status.set({}); return {};
}
ii::core::Result<void> LayerPanelModel::chooseBlend(std::string_view key) {
    if (auto valid=writable(); !valid) return valid;
    if (!hasSelection.get()) return ii::core::fail(ErrorCode::closed,"Select a layer first");
    auto entry=std::ranges::find_if(blendTable,[&](const auto &item){return item.name==key;});
    if (entry==blendTable.end()) return ii::core::fail(ErrorCode::invalid_argument,"Unknown blend mode");
    const auto id=currentId_; auto result=document_->setLayerBlend(id,entry->value);
    if (!result) {(void)status.set(result.error().message);synchronize();return result;}
    synchronize(); (void)status.set({}); return {};
}
ii::core::Result<void> LayerPanelModel::createLayer(iiSharedCanvas::LayerKind kind) {
    if (auto valid=writable(); !valid) return valid;
    auto result=document_->createLayer(kind);
    if (!result) {(void)status.set(result.error().message);return std::unexpected(result.error());}
    (void)status.set({});return {};
}
ii::core::Result<void> LayerPanelModel::duplicateSelected() {
    if (auto valid=writable(); !valid) return valid;
    if (!canDuplicate.get()) return ii::core::fail(ErrorCode::closed,"Select a bitmap or vector layer first");
    auto result=document_->duplicateLayer(currentId_);
    if (!result) {(void)status.set(result.error().message);return std::unexpected(result.error());}
    (void)status.set({}); return {};
}
ii::core::Result<void> LayerPanelModel::removeSelected() {
    if (auto valid=writable(); !valid) return valid;
    if (!hasSelection.get()) return ii::core::fail(ErrorCode::closed,"Select a layer first");
    const auto id=currentId_;
    auto result=document_->removeLayer(id);
    if (!result) {(void)status.set(result.error().message);return result;}
    (void)status.set({}); return {};
}
void LayerPanelModel::sizeChanged(double w,double h,double inset) {
    if (!std::isfinite(w)||!std::isfinite(h)||!std::isfinite(inset)||w<0||h<0||w>8192||h>8192||inset<0) return;
    const lvui::SliderConfiguration slider;
    const lvui::ComboBoxConfiguration combo;
    const auto content=std::max(slider.range_inset*2,w-inset);
    (void)width.set(std::max(1.0,w)); (void)height.set(std::max(1.0,h)); (void)contentWidth.set(content);
    (void)fieldWidth.set(std::max(1.0,content/4));
    (void)blendWidth.set(std::max(combo.indicator_size+combo.right_padding,content*2/3));
}
}
