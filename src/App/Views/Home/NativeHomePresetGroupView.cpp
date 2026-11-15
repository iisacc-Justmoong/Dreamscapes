#include "NativeHomePresetGroupView.h"
#include "NativeHomePresetGroup.generated.h"

namespace dreamscapes::home {
ii::core::Result<void> buildHomePresetGroup(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
                                          std::shared_ptr<HomePresetGroupModel> model) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Preset group requires an active model");
    auto host = widgets.content_host(parent, "homePresetGroupHost");
    if (!host) return std::unexpected(host.error());
    if (auto flex = host->set_flex(1); !flex) return flex;
    return host->set_content([model = std::move(model)](ii::gui::WidgetBuilder &builder) {
        auto factories = lvui::native::BackendFactories{}.with<NativeHomePresetGroupBackend>([model] {
            return NativeHomePresetGroupBackend(model);
        });
        return lvui_generated::NativeHomePresetGroup::build(builder, {}, [](auto &) {}, nullptr, factories);
    });
}
}
NativeHomePresetGroupBackend::NativeHomePresetGroupBackend(std::shared_ptr<Model> source)
    : model(std::move(source)), heading(model->heading), choice(model->choice), save(model->save),
      viewportWidth(model->viewportWidth), choiceWidth(model->choiceWidth), saveWidth(model->saveWidth) {}
