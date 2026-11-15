#include "NativeHomePresetSaveView.h"
#include "NativeHomePresetSave.generated.h"

namespace dreamscapes::home {
ii::core::Result<void> buildHomePresetSave(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
                                         std::shared_ptr<HomePresetSaveModel> model) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home preset editor requires an active model");
    auto host = widgets.content_host(parent, "homePresetSaveHost");
    if (!host) return std::unexpected(host.error());
    return host->set_content([host = *host, model = std::move(model)](ii::gui::WidgetBuilder &builder) {
        using View = lvui_generated::NativeHomePresetSave;
        std::shared_ptr<View::Scope> scope;
        auto factories = lvui::native::BackendFactories{}.with<NativeHomePresetSaveBackend>([model] {
            return NativeHomePresetSaveBackend(model);
        });
        if (auto built = View::build(builder, {}, [](auto &) {}, &scope, factories); !built) return built;
        return builder.on_publish([host, weak = std::weak_ptr(scope)] {
            auto owner = weak.lock(); if (!owner) return;
            auto popup = host.content_item("homePresetSavePopover"), anchor = host.content_item("homeSavePreset");
            if (popup && anchor) owner->backend_0.attach(*popup, *anchor);
        });
    });
}
}
NativeHomePresetSaveBackend::NativeHomePresetSaveBackend(std::shared_ptr<Model> source)
    : model(std::move(source)), heading(model->heading), placeholder(model->placeholder), saveLabel(model->saveLabel),
      cancelLabel(model->cancelLabel), draft(model->draft), enabled(model->enabled), canSave(model->canSave) {}
void NativeHomePresetSaveBackend::open() {
    auto anchor = anchor_; auto popup = popup_; auto state = anchor.snapshot();
    if (state && state->visible && state->enabled) (void)popup.open_for(anchor, 0, state->bounds.height);
}
void NativeHomePresetSaveBackend::cancel() { auto popup = popup_; (void)popup.close_popup(); }
void NativeHomePresetSaveBackend::save() {
    auto owner = model; auto popup = popup_;
    if (owner->save()) (void)popup.close_popup();
}
