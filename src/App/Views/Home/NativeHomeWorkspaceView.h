#pragma once
#include "NativeHomeWorkspace.h"
#include <iiGUI>

namespace dreamscapes::home {
[[nodiscard]] ii::core::Result<void> buildHomeWorkspace(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
                                                        std::shared_ptr<HomeWorkspaceModel>);
}
class NativeHomeWorkspaceBackend final {
  public:
    using Model = dreamscapes::home::HomeWorkspaceModel;
    explicit NativeHomeWorkspaceBackend(std::shared_ptr<Model>);
    std::shared_ptr<Model> model;
    std::shared_ptr<ii::gui::NativeViewSource> sidebar, composer, filePair, sessionPair;
    std::string heading, subtitle, openFileLabel, newCanvasLabel, storageLabel;
    ii::core::Observable<bool> &hasSubtitle, &hasError, &canOpenFile, &canNewCanvas;
    ii::core::Observable<double> &windowWidth, &windowHeight, &sidebarWidth, &viewportWidth, &contentWidth, &inset,
        &composerHeight, &filePairHeight, &sessionPairHeight;
    ii::core::Observable<std::string> &status, &error;
    [[nodiscard]] std::function<void(double, double)> sizeHandler(double gap, double expandedInset) {
        return [weak = std::weak_ptr(model), gap, expandedInset](double width, double height) {
            if (auto current = weak.lock()) (void)current->setViewport(width, height, gap, expandedInset);
        };
    }
    void openFile() { (void)model->openFile(); }
    void newCanvas() { (void)model->newCanvas(); }
};
