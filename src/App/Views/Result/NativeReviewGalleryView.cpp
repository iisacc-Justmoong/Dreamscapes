#include "NativeReviewGalleryView.h"
#include "NativeReviewGallery.generated.h"

namespace dreamscapes::review {
ii::core::Result<void> buildReviewGallery(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
                                          const GalleryConfigure &configure) {
    auto wrapper =
        widgets.vstack(parent, "review-shortcuts", {.spacing = 0, .alignment = ii::gui::StackAlignment::stretch});
    if (!wrapper)
        return std::unexpected(wrapper.error());
    if (auto flex = wrapper->set_flex(1); !flex)
        return flex;
    auto model = std::make_shared<std::weak_ptr<ReviewGalleryModel>>();
    auto input = wrapper->set_input_handler([model](const ii::gui::WidgetInput &event) -> ii::core::Result<bool> {
        if (event.kind != ii::gui::WidgetInputKind::key_down || event.text_input_active ||
            event.native_control_active || event.native_navigation_active || event.alt || event.control || event.meta)
            return false;
        const auto owner = model->lock();
        if (!owner)
            return false;
        if (event.key != ii::gui::WidgetKey::character)
            return false;
        if (event.text == "f" || event.text == "F")
            return owner->session()->key({.key = vm::ReviewKey::shortlist}, false).accepted;
        if (event.text == "x" || event.text == "X")
            return owner->session()->key({.key = vm::ReviewKey::reject}, false).accepted;
        if (event.text == "c" || event.text == "C") {
            if (owner->canCompare.get())
                owner->compare();
            return true;
        }
        return false;
    });
    if (!input)
        return input;
    return lvui_generated::NativeReviewGallery::build(widgets, *wrapper, [&](auto &scope) {
        scope.backend_1.bindNavigation(scope.backend_0.shared());
        *model = scope.backend_1.model;
        scope.backend_0.setDetailHandler([weak = *model] {
            if (auto owner = weak.lock())
                owner->detail();
        });
        if (configure)
            configure(scope.backend_1.model);
    });
}
} // namespace dreamscapes::review
