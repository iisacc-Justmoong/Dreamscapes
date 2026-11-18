#include "NativeTitleBarView.h"
#include "NativeTitleBar.generated.h"

namespace dreamscapes::native {
ii::core::Result<void> buildNativeTitleBar(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,
                                          std::shared_ptr<NativeTitleBarModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Title bar requires a model");
    auto factories=lvui::native::BackendFactories{}.with<NativeTitleBarBackend>([model] {return NativeTitleBarBackend(model);});
    return lvui_generated::NativeTitleBar::build(builder,parent,[](auto &) {},nullptr,factories);
}
}
