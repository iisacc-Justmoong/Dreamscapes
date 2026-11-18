#include "NativeHomeComposerSeedView.h"
#include "NativeHomeComposerSeed.generated.h"

namespace dreamscapes::home {
ii::core::Result<void> buildHomeComposerSeed(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
        std::shared_ptr<HomeComposerSeedModel> model, std::shared_ptr<HomeComposerPopups> popups) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home seed requires an active model");
    auto host = widgets.content_host(parent, "homeComposerSeedHost");
    if (!host) return std::unexpected(host.error());
    if (auto flex = host->set_flex(1); !flex) return flex;
    return host->set_content([host = *host, model = std::move(model), popups = std::move(popups)](ii::gui::WidgetBuilder &builder) {
        using View = lvui_generated::NativeHomeComposerSeed;
        std::shared_ptr<View::Scope> scope;
        auto factories = lvui::native::BackendFactories{}.with<NativeHomeComposerSeedBackend>([model, popups] {
            return NativeHomeComposerSeedBackend(model, popups);
        });
        if (auto built = View::build(builder, {}, [](auto &) {}, &scope, factories); !built) return built;
        return builder.on_publish([host, weak = std::weak_ptr(scope)] {
            auto owner = weak.lock(); if (!owner) return;
            auto popup = host.content_item("homeSeedPopover"), anchor = host.content_item("homeSeedButton");
            if (popup && anchor) owner->backend_0.attach(*popup, *anchor);
        });
    });
}
}
NativeHomeComposerSeedBackend::NativeHomeComposerSeedBackend(std::shared_ptr<Model> source,
        std::shared_ptr<dreamscapes::home::HomeComposerPopups> popups)
    : model(std::move(source)), heading(model->heading), randomLabel(model->randomLabel), applyLabel(model->applyLabel), showHeading(model->showHeading),
      value(model->value), draft(model->draft), enabled(model->enabled), viewportWidth(model->viewportWidth),
      popups_(popups ? std::move(popups) : std::make_shared<dreamscapes::home::HomeComposerPopups>()) {}
void NativeHomeComposerSeedBackend::open() {
    auto popups = popups_;
    popups->toggle(popup_, anchor_, [owner = model] { return owner->begin() && owner->active(); });
}
void NativeHomeComposerSeedBackend::apply() {
    auto owner = model; auto popup = popup_;
    auto popups = popups_;
    if (owner->apply()) popups->close(popup);
}
void NativeHomeComposerSeedBackend::randomize() {
    auto owner = model; auto popup = popup_;
    auto popups = popups_;
    if (owner->randomize()) popups->close(popup);
}
