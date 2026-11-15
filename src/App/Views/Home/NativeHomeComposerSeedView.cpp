#include "NativeHomeComposerSeedView.h"
#include "NativeHomeComposerSeed.generated.h"

namespace dreamscapes::home {
ii::core::Result<void> buildHomeComposerSeed(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
                                           std::shared_ptr<HomeComposerSeedModel> model) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home seed requires an active model");
    auto host = widgets.content_host(parent, "homeComposerSeedHost");
    if (!host) return std::unexpected(host.error());
    if (auto flex = host->set_flex(1); !flex) return flex;
    return host->set_content([host = *host, model = std::move(model)](ii::gui::WidgetBuilder &builder) {
        using View = lvui_generated::NativeHomeComposerSeed;
        std::shared_ptr<View::Scope> scope;
        auto factories = lvui::native::BackendFactories{}.with<NativeHomeComposerSeedBackend>([model] {
            return NativeHomeComposerSeedBackend(model);
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
NativeHomeComposerSeedBackend::NativeHomeComposerSeedBackend(std::shared_ptr<Model> source)
    : model(std::move(source)), heading(model->heading), randomLabel(model->randomLabel), applyLabel(model->applyLabel),
      value(model->value), draft(model->draft), enabled(model->enabled), viewportWidth(model->viewportWidth) {}
void NativeHomeComposerSeedBackend::open() {
    auto owner = model; auto anchor = anchor_; auto popup = popup_;
    auto state = anchor.snapshot();
    if (state && state->visible && state->enabled && owner->begin() && owner->active())
        (void)popup.open_for(anchor, 0, state->bounds.height);
}
void NativeHomeComposerSeedBackend::apply() {
    auto owner = model; auto popup = popup_;
    if (owner->apply()) (void)popup.close_popup();
}
void NativeHomeComposerSeedBackend::randomize() {
    auto owner = model; auto popup = popup_;
    if (owner->randomize()) (void)popup.close_popup();
}
