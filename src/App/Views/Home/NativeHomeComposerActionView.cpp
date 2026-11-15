#include "NativeHomeComposerActionView.h"
#include "NativeHomeComposerAction.generated.h"

namespace dreamscapes::home {
ii::core::Result<void> buildHomeComposerAction(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
                                              std::shared_ptr<HomeComposerActionModel> model) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home action requires an active model");
    auto host = widgets.content_host(parent, "homeComposerActionHost");
    if (!host) return std::unexpected(host.error());
    if (auto flex = host->set_flex(1); !flex) return flex;
    return host->set_content([model = std::move(model)](ii::gui::WidgetBuilder &builder) {
        auto factories = lvui::native::BackendFactories{}.with<NativeHomeComposerActionBackend>([model] {
            return NativeHomeComposerActionBackend(model);
        });
        return lvui_generated::NativeHomeComposerAction::build(builder, {}, [](auto &) {}, nullptr, factories);
    });
}
}
NativeHomeComposerActionBackend::NativeHomeComposerActionBackend(std::shared_ptr<Model> source)
    : model(std::move(source)), icon(model->icon), primary(model->primary), showHeading(model->showHeading),
      heading(model->heading), accessible(model->accessible), label(model->label), enabled(model->enabled), viewportWidth(model->viewportWidth) {}
