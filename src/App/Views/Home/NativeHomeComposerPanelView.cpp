#include "NativeHomeComposerPanelView.h"
#include "NativeHomeComposerGrid.h"
#include "NativeHomeComposerPanel.generated.h"

namespace dreamscapes::home {
ii::core::Result<void> buildHomeComposerPanel(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
                                            std::shared_ptr<HomeComposerPanelModel> model) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home composer panel requires an active model");
    auto factories = lvui::native::BackendFactories{}.with<NativeHomeComposerPanelBackend>([model] {
        return NativeHomeComposerPanelBackend(model);
    });
    return lvui_generated::NativeHomeComposerPanel::build(widgets, parent, [](auto &) {}, nullptr, factories);
}
}
NativeHomeComposerPanelBackend::NativeHomeComposerPanelBackend(std::shared_ptr<Model> source)
    : model(std::move(source)), topGrid(dreamscapes::home::homeComposerGridSource(model, dreamscapes::home::HomeComposerGrid::top)),
      settingsGrid(dreamscapes::home::homeComposerGridSource(model, dreamscapes::home::HomeComposerGrid::settings)),
      attachments(model->sources.attachments), viewportWidth(model->viewportWidth), contentWidth(model->contentWidth),
      topHeight(model->topHeight), settingsHeight(model->settingsHeight), errorHeight(model->errorHeight),
      hasAttachments(model->hasAttachments), hasError(model->hasError), error(model->error) {}
