#include "NativeMaskPanelView.h"
#include "NativeMaskPanel.generated.h"
#include <iiGUI>

std::function<void(double,double)> NativeMaskPanelBackend::sizeHandler(double inset,double rowHeight,double maximumListHeight) {
    return [weak=std::weak_ptr(model),inset,rowHeight,maximumListHeight](double width,double height) {
        if (auto value=weak.lock()) value->sizeChanged(width,height,inset,rowHeight,maximumListHeight);
    };
}
void NativeMaskPanelBackend::attachMenu(ii::gui::Widget menu,ii::gui::Widget anchor) {
    menu_=std::move(menu);anchor_=std::move(anchor);
}
void NativeMaskPanelBackend::openCombineMenu() {
    auto anchor=anchor_.snapshot();
    if (anchor && anchor->visible && anchor->enabled) (void)menu_.open_for(anchor_,0,anchor->bounds.height);
}
namespace dreamscapes::editor {
ii::core::Result<void> buildMaskPanel(ii::gui::WidgetBuilder &widgets,ii::gui::Widget parent,std::shared_ptr<MaskPanelModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Mask panel requires a model");
    auto host=widgets.content_host(parent,"maskPanelHost");if (!host) return std::unexpected(host.error());
    if (auto flex=host->set_flex(1);!flex) return flex;
    return host->set_content([host=*host,model=std::move(model)](ii::gui::WidgetBuilder &builder) {
        using View=lvui_generated::NativeMaskPanel;
        std::shared_ptr<View::Scope> scope;
        auto factories=lvui::native::BackendFactories{}.with<NativeMaskPanelBackend>([model]{return NativeMaskPanelBackend(model);});
        if (auto built=View::build(builder,{},[](auto &) {},&scope,factories);!built) return built;
        return builder.on_publish([host,weak=std::weak_ptr(scope)] {
            if (auto owner=weak.lock()) {
                auto menu=host.content_item("maskCombineMenu"),anchor=host.content_item("maskCombine");
                if (menu && anchor) owner->backend_0.attachMenu(*menu,*anchor);
            }
        });
    });
}
std::shared_ptr<ii::gui::NativeViewSource> maskPanelSource(std::shared_ptr<MaskPanelModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder) {
        return buildMaskPanel(builder,{},model);
    });
}
}
