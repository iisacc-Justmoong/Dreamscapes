#include "NativeCanvasPanelView.h"
#include "NativeCanvasPanel.generated.h"
#include <iiGUI/CompiledViewSource.h>

std::function<void(double,double)> NativeCanvasPanelBackend::sizeHandler(double inset) {
    return [weak=std::weak_ptr(model),inset](double width,double height) {
        if (auto value=weak.lock()) value->sizeChanged(width,height,inset);
    };
}
namespace dreamscapes::editor {
ii::core::Result<void> buildCanvasPanel(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<CanvasPanelModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Canvas panel requires a model");
    auto factories=lvui::native::BackendFactories{}.with<NativeCanvasPanelBackend>([model]{return NativeCanvasPanelBackend(model);});
    return lvui_generated::NativeCanvasPanel::build(builder,parent,[](auto &) {},nullptr,factories);
}
std::shared_ptr<ii::gui::NativeViewSource> canvasPanelSource(std::shared_ptr<CanvasPanelModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder) {
        return buildCanvasPanel(builder,{},model);
    });
}
}
