#include "NativeHomeComposerChoiceView.h"
#include "NativeHomeComposerChoice.generated.h"

namespace dreamscapes::home {
ii::core::Result<void> buildHomeComposerChoice(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
        std::shared_ptr<HomeComposerChoiceModel> model, std::shared_ptr<HomeComposerPopups> popups) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home choice requires an active model");
    if (auto appearance = widgets.appearance(); appearance) {
        if (auto result = model->setFontFamily(appearance->font_family); !result) return result;
    } else return std::unexpected(appearance.error());
    auto host = widgets.content_host(parent, "homeComposerChoiceHost");
    if (!host) return std::unexpected(host.error());
    if (auto flex = host->set_flex(1); !flex) return flex;
    return host->set_content([host = *host, model = std::move(model), popups = std::move(popups)](ii::gui::WidgetBuilder &builder) {
        using View = lvui_generated::NativeHomeComposerChoice;
        std::shared_ptr<View::Scope> scope;
        auto factories = lvui::native::BackendFactories{}.with<NativeHomeComposerChoiceBackend>([model, popups] {
            return NativeHomeComposerChoiceBackend(model, popups);
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
NativeHomeComposerChoiceBackend::NativeHomeComposerChoiceBackend(std::shared_ptr<Model> source,
        std::shared_ptr<dreamscapes::home::HomeComposerPopups> popups)
    : model(std::move(source)), heading(model->heading), showHeading(model->showHeading), value(model->value),
      choices(model->choices), enabled(model->enabled), viewportWidth(model->viewportWidth), menuItemWidth(model->menuItemWidth),
      popups_(popups ? std::move(popups) : std::make_shared<dreamscapes::home::HomeComposerPopups>()) {}
void NativeHomeComposerChoiceBackend::openMenu() {
    auto popups = popups_;
    popups->toggle(menu_, anchor_);
}
void NativeHomeComposerChoiceBackend::choose(std::string key) {
    // The controller may synchronously refresh the choices and invalidate the
    // menu's pending auto-close plan, so close the popup after a successful choice.
    auto source = model;
    auto popup = menu_;
    auto popups = popups_;
    if (source->choose(std::move(key))) popups->close(popup);
}
