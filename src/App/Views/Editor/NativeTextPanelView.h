#pragma once
#include "NativeTextTool.h"
#include <LVUI>
#include <iiGUI>

namespace dreamscapes::editor {
struct TextPanelLayout final {
    ii::core::Observable<double> width{lvui::SliderConfiguration{}.width},height{1},contentWidth{lvui::SliderConfiguration{}.width};
};
[[nodiscard]] ii::core::Result<void> buildTextPanel(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<TextToolModel>);
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> textPanelSource(std::shared_ptr<TextToolModel>);
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> textSurfaceSource(std::shared_ptr<TextToolModel>);
}
class NativeTextPanelBackend final {
  public:
    NativeTextPanelBackend(std::shared_ptr<dreamscapes::editor::TextToolModel>,std::shared_ptr<dreamscapes::editor::TextPanelLayout>);
    std::function<void(double,double)> sizeHandler(double);
    void contentEdited(std::string);
    void familyEdited(std::string);
    void familySubmitted();
    void colorEdited(std::string);
    void colorSubmitted();
    void sizeMoved(double);
    std::shared_ptr<dreamscapes::editor::TextToolModel> model;
    std::shared_ptr<dreamscapes::editor::TextPanelLayout> layout;
    ii::core::Observable<double> &panelWidth,&panelHeight,&contentWidth,&size;
    ii::core::Observable<std::string> &content,&status;
    ii::core::Observable<std::string> familyDraft,colorDraft;
    const std::string placeholder;
    const double minimum,maximum,step;
};
