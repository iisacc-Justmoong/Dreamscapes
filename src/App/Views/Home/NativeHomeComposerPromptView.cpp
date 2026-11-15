#include "NativeHomeComposerPromptView.h"
#include "NativeHomeComposerPrompt.generated.h"

namespace dreamscapes::home {
ii::core::Result<void> buildHomeComposerPrompt(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
                                             std::shared_ptr<HomeComposerPromptModel> model) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home prompt requires an active model");
    auto host = widgets.content_host(parent, "homeComposerPromptHost");
    if (!host) return std::unexpected(host.error());
    if (auto flex = host->set_flex(1); !flex) return flex;
    return host->set_content([model = std::move(model)](ii::gui::WidgetBuilder &builder) {
        auto factories = lvui::native::BackendFactories{}.with<NativeHomeComposerPromptBackend>([model] {
            return NativeHomeComposerPromptBackend(model);
        });
        return lvui_generated::NativeHomeComposerPrompt::build(builder, {}, [](auto &) {}, nullptr, factories);
    });
}
}
NativeHomeComposerPromptBackend::NativeHomeComposerPromptBackend(std::shared_ptr<Model> value)
    : model(std::move(value)), placeholder(model->placeholder), accessible(model->accessible),
      clearAccessible(model->clearAccessible), text(model->text), clearVisible(model->clearVisible),
      enabled(model->enabled), viewportWidth(model->viewportWidth), viewportHeight(model->viewportHeight), editorWidth(model->editorWidth) {}
