#include "NativeHomeNegativePromptView.h"
#include "NativeHomeComposerPromptView.h"
#include "NativeHomeNegativePrompt.generated.h"
#include <iiGUI/CompiledViewSource.h>

namespace dreamscapes::home {
ii::core::Result<void> buildHomeNegativePrompt(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
                                             std::shared_ptr<HomeNegativePromptModel> model) {
    if (!model || !model->active() || !model->prompt() || !model->prompt()->active())
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home negative prompt requires an active draft");
    auto host = widgets.content_host(parent, "homeNegativePromptHost");
    if (!host) return std::unexpected(host.error());
    if (auto flex = host->set_flex(1); !flex) return flex;
    return host->set_content([host = *host, model = std::move(model)](ii::gui::WidgetBuilder &builder) {
        using View = lvui_generated::NativeHomeNegativePrompt;
        std::shared_ptr<View::Scope> scope;
        auto factories = lvui::native::BackendFactories{}.with<NativeHomeNegativePromptBackend>([model] {
            return NativeHomeNegativePromptBackend(model);
        });
        if (auto built = View::build(builder, {}, [](auto &) {}, &scope, factories); !built) return built;
        return builder.on_publish([host, weak = std::weak_ptr(scope)] {
            auto owner = weak.lock(); if (!owner) return;
            auto popup = host.content_item("homeNegativePopover"), anchor = host.content_item("homeNegativeButton");
            if (popup && anchor) owner->backend_0.attach(*popup, *anchor);
        });
    });
}
}
NativeHomeNegativePromptBackend::NativeHomeNegativePromptBackend(std::shared_ptr<Model> source)
    : model(std::move(source)),
      promptSource(ii::gui::compiled_view_source([prompt = model->prompt()](ii::gui::WidgetBuilder &builder) {
          return dreamscapes::home::buildHomeComposerPrompt(builder, {}, prompt);
      })), heading(model->heading), doneLabel(model->doneLabel), enabled(model->enabled), viewportWidth(model->viewportWidth) {}
void NativeHomeNegativePromptBackend::open() {
    auto anchor = anchor_; auto popup = popup_; auto state = anchor.snapshot();
    if (state && state->visible && state->enabled) (void)popup.open_for(anchor, 0, state->bounds.height);
}
void NativeHomeNegativePromptBackend::done() { auto popup = popup_; (void)popup.close_popup(); }
