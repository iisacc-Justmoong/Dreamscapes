#include "NativeFilePanelView.h"
#include "NativeFilePanel.generated.h"
#include <iiGUI/CompiledViewSource.h>

std::function<void(double,double)> NativeFilePanelBackend::sizeHandler(double inset) {
    return [weak=std::weak_ptr(model),inset](double width,double height) {
        if (auto self=weak.lock()) self->sizeChanged(width,height,inset);
    };
}
namespace dreamscapes::editor {
ii::core::Result<void> buildFilePanel(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<FilePanelModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"File inspector requires a model");
    auto factories=lvui::native::BackendFactories{}.with<NativeFilePanelBackend>([model]{return NativeFilePanelBackend(model);});
    return lvui_generated::NativeFilePanel::build(builder,parent,[](auto &) {},nullptr,factories);
}
std::shared_ptr<ii::gui::NativeViewSource> filePanelSource(std::shared_ptr<FilePanelModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder) {
        return buildFilePanel(builder,{},model);
    });
}
}
