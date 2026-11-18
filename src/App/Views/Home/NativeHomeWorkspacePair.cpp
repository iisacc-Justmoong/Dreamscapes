#include "NativeHomeWorkspacePair.h"
#include <LVUI>
#include <iiGUI>
#include <algorithm>

namespace dreamscapes::home {
namespace {
ii::core::Result<void> placePair(ii::gui::Widget root, ii::gui::NativeView first, ii::gui::NativeView second,
                                HomeWorkspacePair kind, const HomeWorkspaceGeometry &g) {
    const auto extent = [&](double value) { return static_cast<float>(std::max(1.0, value)); };
    const bool files = kind == HomeWorkspacePair::files;
    const double pairHeight = files ? (g.wide ? g.fileHeight : 2 * g.fileHeight + g.gap) : (g.wide ? 160 : 320 + g.gap);
    if (auto r = root.set_extent({g.content, pairHeight}); !r) return r;
    ii::gui::Rect firstFrame, secondFrame;
    if (files) {
        firstFrame = {float(g.gap), float(g.gap), extent(g.file - 2*g.gap), extent(g.fileHeight - 2*g.gap)};
        secondFrame = {float(g.wide ? g.file + 2*g.gap : g.gap), float(g.wide ? g.gap : g.fileHeight + 2*g.gap),
                       extent(g.selected - 2*g.gap), extent(g.fileHeight - 2*g.gap)};
    } else {
        firstFrame = {0, 0, extent(g.queue), 160};
        secondFrame = {float(g.wide ? g.queue + 2*g.gap : g.gap), float(g.wide ? g.gap : 160 + 2*g.gap),
                       extent(g.generations - 2*g.gap), extent(160 - 2*g.gap)};
    }
    if (auto r = first.root().set_frame(firstFrame); !r) return r;
    return second.root().set_frame(secondFrame);
}
}
std::shared_ptr<ii::gui::NativeViewSource> homeWorkspacePairSource(std::weak_ptr<HomeWorkspaceModel> weak,
                                                                   HomeWorkspacePair kind) {
    return ii::gui::compiled_view_source([weak = std::move(weak), kind](ii::gui::WidgetBuilder &builder) -> ii::core::Result<void> {
        auto model = weak.lock();
        if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::closed, "Home workspace pair has no active model");
        auto root = builder.absolute_stack({}, kind == HomeWorkspacePair::files ? "homeFilePair" : "homeSessionPair", {});
        if (!root) return std::unexpected(root.error());
        const bool files = kind == HomeWorkspacePair::files;
        auto first = ii::gui::NativeView::create(builder, *root, files ? "homeRecentFiles" : "homeQueue",
            {.source = files ? model->sources.recentFiles : model->sources.queue});
        if (!first) return std::unexpected(first.error());
        auto second = ii::gui::NativeView::create(builder, *root, files ? "homeSelectedFile" : "homeRecentGenerations",
            {.source = files ? model->sources.selectedFile : model->sources.recentGenerations});
        if (!second) return std::unexpected(second.error());
        auto bindings = std::make_shared<lvui::native::Bindings>();
        if (auto retained = builder.retain(bindings); !retained) return retained;
        if (auto retiring = builder.on_retire([weakBindings = std::weak_ptr(bindings)] {
                if (auto current = weakBindings.lock()) (void)current->close();
            }); !retiring) return retiring;
        auto binding = bindings->bind([model](lvui::native::ReadContext &context) { return context.read(model->geometry); },
            [root = *root, first = *first, second = *second, kind](const HomeWorkspaceGeometry &geometry) {
                return placePair(root, first, second, kind, geometry);
            });
        if (!binding) return std::unexpected(binding.error());
        return {};
    });
}
} // namespace dreamscapes::home
