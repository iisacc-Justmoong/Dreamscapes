#pragma once
#include "NativeGenerationWorkspace.h"
#include <iiGUI/NativeWidgets.h>

namespace dreamscapes::generation {
[[nodiscard]] ii::core::Result<void> buildGenerationWorkspace(ii::gui::WidgetBuilder &,ii::gui::Widget,
    std::shared_ptr<GenerationWorkspaceModel>);
}
class NativeGenerationWorkspaceBackend final {
  public:
    using Model=dreamscapes::generation::GenerationWorkspaceModel;
    explicit NativeGenerationWorkspaceBackend(std::shared_ptr<Model> value)
        : model(std::move(value)),fields(model->fields),canGenerate(model->canGenerate),
          width(model->viewportWidth),height(model->viewportHeight),panelWidth(model->panelWidth),
          previewWidth(model->previewWidth),showPreview(model->showPreview),
          heading(model->video?"Create video":"Create image"),media(model->video?"Video":"Image") {}
    std::function<void(double,double)> sizeHandler(double maximumPanel,double minimumPreview);
    std::shared_ptr<Model> model;
    ii::core::Observable<std::vector<Model::Field>> &fields;
    ii::core::Observable<bool> &canGenerate;
    ii::core::Observable<double> &width,&height,&panelWidth,&previewWidth;
    ii::core::Observable<bool> &showPreview;
    std::string heading,media;
};
