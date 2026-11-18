#include "NativeAudioPanelView.h"
#include "NativeAudioPanel.generated.h"
#include <iiGUI>

std::function<void(double,double)> NativeAudioPanelBackend::sizeHandler(double inset,double maximumField) {
    return [weak=std::weak_ptr(model),inset,maximumField](double width,double height) {if (auto value=weak.lock()) value->sizeChanged(width,height,inset,maximumField);};
}
namespace dreamscapes::editor {
ii::core::Result<void> buildAudioPanel(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<AudioPanelModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Audio panel requires a model");
    auto host=builder.content_host(parent,"audioPanelHost");if (!host) return std::unexpected(host.error());
    if (auto flex=host->set_flex(1);!flex) return flex;
    return host->set_content([model=std::move(model)](ii::gui::WidgetBuilder &widgets) {
        auto factories=lvui::native::BackendFactories{}.with<NativeAudioPanelBackend>([model]{return NativeAudioPanelBackend(model);});
        return lvui_generated::NativeAudioPanel::build(widgets,{},[](auto &) {},nullptr,factories);
    });
}
std::shared_ptr<ii::gui::NativeViewSource> audioPanelSource(std::shared_ptr<AudioPanelModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder){return buildAudioPanel(builder,{},model);});
}
}
