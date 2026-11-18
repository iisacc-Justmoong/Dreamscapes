#include "NativeHomeComposerGrid.h"
#include <LVUI>
#include <iiGUI>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace dreamscapes::home {
namespace {
using GridChild = std::pair<std::size_t, ii::gui::NativeView>;
ii::core::Result<void> place(ii::gui::Widget root, const std::vector<GridChild> &children,
                             HomeComposerGrid kind, const HomeComposerLayout &layout) {
    const auto height = kind == HomeComposerGrid::top ? layout.topHeight : layout.settingsHeight;
    if (layout.contentWidth <= 0 || height <= 0) return {};
    if (auto result = root.set_extent({layout.contentWidth, height}); !result) return result;
    for (const auto &[slot, child] : children) {
        const auto &frame = kind == HomeComposerGrid::top ? layout.top[slot] : layout.settings[slot];
        if (frame.visible) {
            if (auto result = child.root().set_frame({float(frame.x),float(frame.y),float(frame.width),float(frame.height)}); !result) return result;
        }
        if (auto result = child.root().set_visible(frame.visible); !result) return result;
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
        const auto &sources = model->sources;
        const bool compact = model->compactControls(), top = kind == HomeComposerGrid::top;
        const auto count = top ? (compact ? std::size_t{3} : sources.top.size()) :
            sources.settings.size() + (compact ? 2 : 0);
        std::vector<GridChild> children; children.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            if (compact && !top && (i == 5 || i == 6)) continue;
            static constexpr std::array<std::size_t,2> compactActions{2,5};
            const auto &source = top ? (compact && i == 2 ? sources.inlineNegative : sources.top[i]) :
                i < sources.settings.size() ? sources.settings[i] :
                    sources.top[compactActions[i-sources.settings.size()]];
            auto child = ii::gui::NativeView::create(builder, *root,
                (top ? "homeComposerTop" : "homeComposerSetting") + std::to_string(i),
                {.source = source, .clip = false});
            if (!child) return std::unexpected(child.error());
            children.emplace_back(i, *child);
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
