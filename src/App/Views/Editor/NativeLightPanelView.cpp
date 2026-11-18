#include "NativeLightPanelView.h"
#include "NativeLightPanel.generated.h"
#include <iiGUI>

std::function<void(double,double)> NativeLightPanelBackend::sizeHandler(double inset,double valueWidth,double rowHeight,double rowPadding) {
    return [weak=std::weak_ptr(model),inset,valueWidth,rowHeight,rowPadding](double width,double height) {
        if (auto value=weak.lock()) value->sizeChanged(width,height,inset,valueWidth,rowHeight,rowPadding);
    };
}
namespace dreamscapes::editor {
ii::core::Result<void> buildLightPanel(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<LightPanelModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Light panel requires a model");
    auto host=builder.content_host(parent,"lightPanelHost");if (!host) return std::unexpected(host.error());
    if (auto flex=host->set_flex(1);!flex) return flex;
    return host->set_content([model=std::move(model)](ii::gui::WidgetBuilder &widgets) {
        auto factories=lvui::native::BackendFactories{}.with<NativeLightPanelBackend>([model]{return NativeLightPanelBackend(model);});
        return lvui_generated::NativeLightPanel::build(widgets,{},[](auto &) {},nullptr,factories);
    });
}
std::shared_ptr<ii::gui::NativeViewSource> lightPanelSource(std::shared_ptr<LightPanelModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder){return buildLightPanel(builder,{},model);});
}
}
