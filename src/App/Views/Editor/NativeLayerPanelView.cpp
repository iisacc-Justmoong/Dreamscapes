#include "NativeLayerPanelView.h"
#include "NativeLayerPanel.generated.h"
#include <iiGUI>

std::function<void(double,double)> NativeLayerPanelBackend::sizeHandler(double inset) {
    return [weak=std::weak_ptr(model),inset](double width,double height) {
        if (auto self=weak.lock()) self->sizeChanged(width,height,inset);
    };
}
void NativeLayerPanelBackend::attachMenu(ii::gui::Widget menu,ii::gui::Widget anchor) {
    menu_=std::move(menu); anchor_=std::move(anchor);
}
void NativeLayerPanelBackend::openBlendMenu() {
    auto anchor=anchor_.snapshot();
    if (anchor && anchor->visible && anchor->enabled) (void)menu_.open_for(anchor_,0,anchor->bounds.height);
}
void NativeLayerPanelBackend::attachNewMenu(ii::gui::Widget menu,ii::gui::Widget anchor) {
    newMenu_=std::move(menu);newAnchor_=std::move(anchor);
}
void NativeLayerPanelBackend::openNewMenu() {
    auto anchor=newAnchor_.snapshot();
    if (anchor && anchor->visible && anchor->enabled) (void)newMenu_.open_for(newAnchor_,0,anchor->bounds.height);
}
namespace dreamscapes::editor {
ii::core::Result<void> buildLayerPanel(ii::gui::WidgetBuilder &widgets,ii::gui::Widget parent,std::shared_ptr<LayerPanelModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Layer panel requires a model");
    auto host=widgets.content_host(parent,"layerPanelHost");
    if (!host) return std::unexpected(host.error());
    if (auto flex=host->set_flex(1); !flex) return flex;
    return host->set_content([host=*host,model=std::move(model)](ii::gui::WidgetBuilder &builder) {
        using View=lvui_generated::NativeLayerPanel;
        std::shared_ptr<View::Scope> scope;
        auto factories=lvui::native::BackendFactories{}.with<NativeLayerPanelBackend>([model]{return NativeLayerPanelBackend(model);});
        if (auto built=View::build(builder,{},[](auto &) {},&scope,factories); !built) return built;
        return builder.on_publish([host,weak=std::weak_ptr(scope)] {
            if (auto owner=weak.lock()) {
                auto menu=host.content_item("layerBlendMenu"),anchor=host.content_item("layerBlend");
                if (menu && anchor) owner->backend_0.attachMenu(*menu,*anchor);
                auto newMenu=host.content_item("layerNewMenu"),newAnchor=host.content_item("editorNewLayer");
                if (newMenu && newAnchor) owner->backend_0.attachNewMenu(*newMenu,*newAnchor);
            }
        });
    });
}
std::shared_ptr<ii::gui::NativeViewSource> layerPanelSource(std::shared_ptr<LayerPanelModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder) {
        return buildLayerPanel(builder,{},model);
    });
}
}
