#include "NativeBrushPanelView.h"
#include "NativeBrushPanel.generated.h"
#include "NativeAssets.h"
#include "App/ViewModels/Native/ViewValue.h"
#include <iiGUI>
#include <iiJson>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dreamscapes::editor {
BrushPanelSpec brushPanelSpec() {
    const auto parsed=ii::json::parse(native::toolCatalog);
    if (!parsed) throw std::invalid_argument("Invalid brush control catalog");
    const auto &tools=vm::array(*parsed);
    const auto found=std::ranges::find_if(tools,[](const auto &entry){return vm::text(vm::get(entry,"key"))=="brush";});
    if (found==tools.end()) throw std::invalid_argument("Brush controls are missing");
    const auto &fields=vm::array(vm::get(*found,"fields"));
    auto read=[&](std::string_view id) {
        const auto item=std::ranges::find_if(fields,[&](const auto &entry){return vm::text(vm::get(entry,"id"))==id;});
        if (item==fields.end()) throw std::invalid_argument("Brush control is missing");
        BrushControlSpec result{vm::text(vm::get(*item,"label")),vm::number(vm::get(*item,"minimum")),
            vm::number(vm::get(*item,"maximum")),vm::number(vm::get(*item,"step")),vm::number(vm::get(*item,"initial"))};
        if (result.label.empty() || !std::isfinite(result.minimum) || !std::isfinite(result.maximum) ||
            !std::isfinite(result.step) || !std::isfinite(result.initial) || result.step<=0 ||
            result.minimum>=result.maximum || result.initial<result.minimum || result.initial>result.maximum)
            throw std::invalid_argument("Invalid brush control range");
        return result;
    };
    auto result=BrushPanelSpec{read("field-4"),read("field-5"),read("field-8"),read("field-9")};
    result.size.minimum=std::max(1.0,result.size.minimum); // iiPaintEngine requires a positive diameter.
    return result;
}
ii::core::Result<void> buildBrushPanel(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<BrushToolModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Brush panel requires a model");
    auto host=builder.content_host(parent,"brushPanelHost");if (!host) return std::unexpected(host.error());
    if (auto flex=host->set_flex(1);!flex) return flex;
    auto layout=std::make_shared<BrushPanelLayout>();
    return host->set_content([model=std::move(model),layout=std::move(layout)](ii::gui::WidgetBuilder &widgets) {
        auto factories=lvui::native::BackendFactories{}.with<NativeBrushPanelBackend>([model,layout]{return NativeBrushPanelBackend(model,layout);});
        return lvui_generated::NativeBrushPanel::build(widgets,{},[](auto &) {},nullptr,factories);
    });
}
std::shared_ptr<ii::gui::NativeViewSource> brushPanelSource(std::shared_ptr<BrushToolModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder){return buildBrushPanel(builder,{},model);});
}
}
NativeBrushPanelBackend::NativeBrushPanelBackend(std::shared_ptr<dreamscapes::editor::BrushToolModel> value,
    std::shared_ptr<dreamscapes::editor::BrushPanelLayout> dimensions)
    :model(std::move(value)),layout(std::move(dimensions)),panelWidth(layout->width),panelHeight(layout->height),contentWidth(layout->contentWidth),
     size(model->size),hardness(model->hardness),opacity(model->opacity),flow(model->flow),
     colorText(model->colorText),status(model->status),availability(model->availability),spec(dreamscapes::editor::brushPanelSpec()),
     sizeLabel(spec.size.label),hardnessLabel(spec.hardness.label),opacityLabel(spec.opacity.label),flowLabel(spec.flow.label),
     sizeMin(spec.size.minimum),sizeMax(spec.size.maximum),sizeStep(spec.size.step),
     hardnessMin(spec.hardness.minimum),hardnessMax(spec.hardness.maximum),hardnessStep(spec.hardness.step),
     opacityMin(spec.opacity.minimum),opacityMax(spec.opacity.maximum),opacityStep(spec.opacity.step),
     flowMin(spec.flow.minimum),flowMax(spec.flow.maximum),flowStep(spec.flow.step) {}
std::function<void(double,double)> NativeBrushPanelBackend::sizeHandler(double inset) {
    return [layout=std::weak_ptr(layout),inset](double width,double height) {
        if (auto value=layout.lock();value && std::isfinite(width) && std::isfinite(height) && width>=0 && height>=0) {
            (void)value->width.set(width);(void)value->height.set(height);
            (void)value->contentWidth.set(std::clamp(width-inset,lvui::SliderConfiguration{}.thumb_diameter,8192.0));
        }
    };
}
