#include "NativeHomeComposerChoiceView.h"
#include "NativeHomeComposerChoice.generated.h"

namespace dreamscapes::home {
ii::core::Result<void> buildHomeComposerChoice(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
                                             std::shared_ptr<HomeComposerChoiceModel> model) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home choice requires an active model");
    auto host = widgets.content_host(parent, "homeComposerChoiceHost");
    if (!host) return std::unexpected(host.error());
    if (auto flex = host->set_flex(1); !flex) return flex;
    return host->set_content([host = *host, model = std::move(model)](ii::gui::WidgetBuilder &builder) {
        using View = lvui_generated::NativeHomeComposerChoice;
        std::shared_ptr<View::Scope> scope;
        auto factories = lvui::native::BackendFactories{}.with<NativeHomeComposerChoiceBackend>([model] {
            return NativeHomeComposerChoiceBackend(model);
        });
        if (auto built = View::build(builder, {}, [](auto &) {}, &scope, factories); !built) return built;
        return builder.on_publish([host, weak = std::weak_ptr(scope)] {
            auto owner = weak.lock(); if (!owner) return;
            auto menu = host.content_item("homeChoiceMenu"), anchor = host.content_item("homeChoiceButton");
            if (menu && anchor) owner->backend_0.attachMenu(*menu, *anchor);
        });
    });
}
}
NativeHomeComposerChoiceBackend::NativeHomeComposerChoiceBackend(std::shared_ptr<Model> source)
    : model(std::move(source)), heading(model->heading), showHeading(model->showHeading), value(model->value),
      choices(model->choices), enabled(model->enabled), viewportWidth(model->viewportWidth) {}
void NativeHomeComposerChoiceBackend::openMenu() {
    auto anchor = anchor_.snapshot();
    if (anchor && anchor->visible && anchor->enabled) (void)menu_.open_for(anchor_, 0, anchor->bounds.height);
}
