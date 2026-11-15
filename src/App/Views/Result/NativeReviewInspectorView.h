#pragma once
#include "NativeReviewInspectorViewModel.h"
#include <LVUI/NativeImageResolver.h>
#include <iiGUI/CompiledViewSource.h>
#include <iiGUI/Image.h>
#include <iiGUI/NativeWidgets.h>
#include <memory>

namespace dreamscapes::review {
[[nodiscard]] ii::core::Result<void> buildReviewInspector(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
    std::shared_ptr<InspectorViewModel>,
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images = {},
    std::shared_ptr<ii::gui::NativeViewSource> mediaView = {});
}
class NativeReviewInspectorBackend final {
  public:
    using Model = dreamscapes::review::InspectorViewModel;
    NativeReviewInspectorBackend(std::shared_ptr<Model>, std::shared_ptr<ii::gui::NativeViewSource>);
    std::shared_ptr<Model> model;
    std::shared_ptr<ii::gui::NativeViewSource> mediaView;
    bool hasMediaView;
    const std::string &heading, &previewLabel, &expandLabel, &saveLabel, &noResult;
    ii::core::Observable<std::vector<dreamscapes::review::InspectorDisplayField::Ptr>> &fields;
    ii::core::Observable<std::string> &selectionSummary, &previewSource, &jobText, &originalText;
    ii::core::Observable<bool> &canExpand, &canSave, &jobVisible, &noPreview;
    ii::core::Observable<double> &viewportWidth, &viewportHeight, &previewHeight;
    void sizeChanged(double width, double height);
    void expand() { model->expand(); }
    void save() { model->save(); }
};
