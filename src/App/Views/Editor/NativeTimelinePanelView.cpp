#include "NativeTimelinePanelView.h"
#include <iiGUI/CompiledViewSource.h>

namespace dreamscapes::editor {
namespace {
struct Binding final {
    Binding(std::shared_ptr<TimelinePanelModel> value,ii::gui::NativeEditorTimeline control)
        : model(std::move(value)),timeline(std::move(control)) {}
    std::shared_ptr<TimelinePanelModel> model;
    ii::gui::NativeEditorTimeline timeline;
    ii::core::ObservableSubscription options;
};
}
std::shared_ptr<ii::gui::NativeViewSource> timelinePanelSource(std::shared_ptr<TimelinePanelModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder) -> ii::core::Result<void> {
        if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Timeline requires a view model");
        const std::weak_ptr<TimelinePanelModel> weak=model;
        ii::gui::NativeEditorTimelineCallbacks callbacks;
        callbacks.query=[weak](const auto &query) -> ii::core::Result<ii::gui::EditorTimelineProjection> {
            if (auto value=weak.lock()) return value->project(query);
            return ii::core::fail(ii::core::ErrorCode::closed,"Timeline is closed");
        };
        callbacks.input=[weak](const auto &event) -> ii::core::Result<void> {
            if (auto value=weak.lock()) {
                auto result=value->input(event);
                if (!result && result.error().code!=ii::core::ErrorCode::wrong_executor) return {};
                return result;
            }
            return ii::core::fail(ii::core::ErrorCode::closed,"Timeline is closed");
        };
        callbacks.viewport_changed=[weak](const auto &viewport) {if (auto value=weak.lock()) value->viewportChanged(viewport);};
        auto control=ii::gui::NativeEditorTimeline::create(builder,{},"editorTimelineCanvas",model->options.get(),std::move(callbacks));
        if (!control) return std::unexpected(control.error());
        auto binding=std::make_shared<Binding>(model,std::move(*control));
        binding->options=model->options.observe([weakBinding=std::weak_ptr(binding)](const auto &value,std::uint64_t) {
            if (auto active=weakBinding.lock()) {
                if (auto result=active->timeline.set_options(value); !result && result.error().code!=ii::core::ErrorCode::closed)
                    (void)active->model->status.set(result.error().message);
            }
        });
        return builder.retain(binding);
    });
}
}
