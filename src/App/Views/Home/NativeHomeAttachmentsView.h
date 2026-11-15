#pragma once
#include "NativeHomeAttachments.h"
#include <LVUI/NativeImageResolver.h>
#include <iiGUI/Image.h>
#include <iiGUI/NativeWidgets.h>

namespace dreamscapes::home {
[[nodiscard]] ii::core::Result<void> buildHomeAttachments(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
    std::shared_ptr<HomeAttachmentsModel>, std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images = {});
}
class NativeHomeAttachmentsBackend final {
  public:
    using Model = dreamscapes::home::HomeAttachmentsModel;
    explicit NativeHomeAttachmentsBackend(std::shared_ptr<Model>);
    std::shared_ptr<Model> model;
    ii::core::Observable<std::vector<dreamscapes::home::HomeAttachmentRow>> &rows;
    ii::core::Observable<bool> &visible, &enabled;
    ii::core::Observable<double> &viewportWidth;
    void sizeChanged(double width, double height) { (void)model->setViewport(width, height); }
    void remove(std::string key) { (void)model->remove(std::move(key)); }
};
