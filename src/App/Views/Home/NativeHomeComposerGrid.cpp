#include "NativeHomeComposerGrid.h"
#include <LVUI/NativeBindings.h>
#include <iiGUI/CompiledViewSource.h>
#include <algorithm>
#include <string>
#include <vector>

namespace dreamscapes::home {
namespace {
ii::core::Result<void> place(ii::gui::Widget root, const std::vector<ii::gui::NativeView> &children,
                             HomeComposerGrid kind, const HomeComposerLayout &layout) {
    const auto height = kind == HomeComposerGrid::top ? layout.topHeight : layout.settingsHeight;
    if (layout.contentWidth <= 0 || height <= 0) return {};
    if (auto result = root.set_extent({layout.contentWidth, height}); !result) return result;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto &frame = kind == HomeComposerGrid::top ? layout.top[i] : layout.settings[i];
        if (frame.visible) {
            if (auto result = children[i].root().set_frame({float(frame.x),float(frame.y),float(frame.width),float(frame.height)}); !result) return result;
        }
        if (auto result = children[i].root().set_visible(frame.visible); !result) return result;
    }
    return {};
}
}
std::shared_ptr<ii::gui::NativeViewSource> homeComposerGridSource(std::weak_ptr<HomeComposerPanelModel> weak, HomeComposerGrid kind) {
    return ii::gui::compiled_view_source([weak = std::move(weak), kind](ii::gui::WidgetBuilder &builder) -> ii::core::Result<void> {
        auto model = weak.lock();
        if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::closed, "Composer grid has no active panel");
        auto root = builder.absolute_stack({}, kind == HomeComposerGrid::top ? "homeComposerTopGrid" : "homeComposerSettingsGrid", {});
        if (!root) return std::unexpected(root.error());
        const auto count = kind == HomeComposerGrid::top ? model->sources.top.size() : model->sources.settings.size();
        std::vector<ii::gui::NativeView> children; children.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto &source = kind == HomeComposerGrid::top ? model->sources.top[i] : model->sources.settings[i];
            auto child = ii::gui::NativeView::create(builder, *root,
                (kind == HomeComposerGrid::top ? "homeComposerTop" : "homeComposerSetting") + std::to_string(i),
                {.source = source, .clip = false});
            if (!child) return std::unexpected(child.error());
            children.push_back(*child);
        }
        auto bindings = std::make_shared<lvui::native::Bindings>();
        if (auto result = builder.retain(bindings); !result) return result;
        if (auto result = builder.on_retire([weakBindings = std::weak_ptr(bindings)] {
                if (auto current = weakBindings.lock()) (void)current->close();
            }); !result) return result;
        auto binding = bindings->bind([model](lvui::native::ReadContext &context) { return context.read(model->layout); },
            [root = *root, children = std::move(children), kind](const HomeComposerLayout &layout) {
                return place(root, children, kind, layout);
            });
        if (!binding) return std::unexpected(binding.error());
        return {};
    });
}
} // namespace dreamscapes::home
