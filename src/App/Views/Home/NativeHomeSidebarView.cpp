#include "NativeHomeSidebarView.h"
#include "NativeHomeSidebar.generated.h"

namespace dreamscapes::home {
ii::core::Result<void> buildHomeSidebar(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
                                       std::shared_ptr<HomeSidebarModel> model) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home sidebar requires an active model");
    auto host = widgets.content_host(parent, "homeSidebarHost");
    if (!host) return std::unexpected(host.error());
    if (auto flex = host->set_flex(1); !flex) return flex;
    return host->set_content([model = std::move(model)](ii::gui::WidgetBuilder &builder) {
        using View = lvui_generated::NativeHomeSidebar;
        auto factories = lvui::native::BackendFactories{}.with<NativeHomeSidebarBackend>([model] { return NativeHomeSidebarBackend(model); });
        return View::build(builder, {}, [](auto &) {}, nullptr, factories);
    });
}
} // namespace dreamscapes::home
NativeHomeSidebarBackend::NativeHomeSidebarBackend()
    : NativeHomeSidebarBackend(dreamscapes::home::HomeSidebarModel::create({})) {}
NativeHomeSidebarBackend::NativeHomeSidebarBackend(std::shared_ptr<Model> source)
    : model(std::move(source)), rows(model->rows), compact(model->compact),
      viewportWidth(model->viewportWidth), viewportHeight(model->viewportHeight) {}
