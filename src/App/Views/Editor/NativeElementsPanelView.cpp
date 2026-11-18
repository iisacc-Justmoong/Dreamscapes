#include "NativeElementsPanelView.h"
#include "NativeElementsPanel.generated.h"
#include <iiGUI>

std::function<void(double,double)> NativeElementsPanelBackend::sizeHandler(double inset,double gap) {
    return [weak=std::weak_ptr(model),inset,gap](double width,double height) {
        if (auto value=weak.lock()) value->sizeChanged(width,height,inset,gap);
    };
}
namespace dreamscapes::editor {
ii::core::Result<void> buildElementsPanel(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<ElementsPanelModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Elements panel requires a model");
    auto factories=lvui::native::BackendFactories{}.with<NativeElementsPanelBackend>([model]{return NativeElementsPanelBackend(model);});
    return lvui_generated::NativeElementsPanel::build(builder,parent,[](auto &) {},nullptr,factories);
}
std::shared_ptr<ii::gui::NativeViewSource> elementsPanelSource(std::shared_ptr<ElementsPanelModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder) {
        return buildElementsPanel(builder,{},model);
    });
}
}
