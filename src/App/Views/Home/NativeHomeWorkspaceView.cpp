#include "NativeHomeWorkspaceView.h"
#include "NativeHomeWorkspacePair.h"
#include "NativeHomeWorkspace.generated.h"

namespace dreamscapes::home {
ii::core::Result<void> buildHomeWorkspace(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
                                        std::shared_ptr<HomeWorkspaceModel> model) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home workspace requires an active model");
    auto factories = lvui::native::BackendFactories{}.with<NativeHomeWorkspaceBackend>([model] { return NativeHomeWorkspaceBackend(model); });
    return lvui_generated::NativeHomeWorkspace::build(widgets, parent, [](auto &) {}, nullptr, factories);
}
}
NativeHomeWorkspaceBackend::NativeHomeWorkspaceBackend(std::shared_ptr<Model> value)
    : model(std::move(value)), sidebar(model->sources.sidebar), composer(model->sources.composer),
      filePair(dreamscapes::home::homeWorkspacePairSource(model, dreamscapes::home::HomeWorkspacePair::files)),
      sessionPair(dreamscapes::home::homeWorkspacePairSource(model, dreamscapes::home::HomeWorkspacePair::session)),
      heading(model->labels.heading), subtitle(model->labels.subtitle),
      openFileLabel(model->labels.openFile), newCanvasLabel(model->labels.newCanvas), storageLabel(model->labels.storage),
      hasSubtitle(model->hasSubtitle), hasError(model->hasError),
      canOpenFile(model->canOpenFile), canNewCanvas(model->canNewCanvas), windowWidth(model->windowWidth),
      windowHeight(model->windowHeight), sidebarWidth(model->sidebarWidth), viewportWidth(model->viewportWidth),
      contentWidth(model->contentWidth), inset(model->inset), composerHeight(model->composerHeight),
      filePairHeight(model->filePairHeight), sessionPairHeight(model->sessionPairHeight),
      status(model->status), error(model->error) {}
