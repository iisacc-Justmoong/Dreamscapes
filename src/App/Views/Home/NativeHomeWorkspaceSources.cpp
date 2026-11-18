#include "NativeHomeWorkspaceSources.h"
#include "NativeHomeQueueView.h"
#include "NativeHomeSidebarView.h"
#include <iiGUI>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
HomeWorkspaceSources homeWorkspaceSources(HomeWorkspaceComponents parts) {
    if (!parts.sidebar || !parts.sidebar->active() || !parts.composer || !parts.recentFiles || !parts.recentFiles->active() ||
        !parts.selectedFile || !parts.selectedFile->active() || !parts.queue || !parts.recentGenerations ||
        !parts.recentGenerations->active() || parts.queueReadySvg.empty())
        throw std::invalid_argument("Home workspace components require active models, a composer and the queue asset");
    HomeWorkspaceSources result;
    result.sidebar = ii::gui::compiled_view_source([model = std::move(parts.sidebar)](ii::gui::WidgetBuilder &builder) {
        return buildHomeSidebar(builder, {}, model);
    });
    result.composer = std::move(parts.composer);
    auto fileImages = parts.recentFileImages ? std::move(parts.recentFileImages) : parts.images;
    auto generationImages = parts.recentGenerationImages ? std::move(parts.recentGenerationImages) : parts.images;
    result.recentFiles = ii::gui::compiled_view_source([model = std::move(parts.recentFiles), images = std::move(fileImages)](ii::gui::WidgetBuilder &builder) {
        return buildHomeRecentFiles(builder, {}, model, images);
    });
    result.selectedFile = ii::gui::compiled_view_source([model = std::move(parts.selectedFile), images = parts.images](ii::gui::WidgetBuilder &builder) {
        return buildHomeSelectedFile(builder, {}, model, images);
    });
    result.queue = ii::gui::compiled_view_source([model = std::move(parts.queue), svg = std::move(parts.queueReadySvg)](ii::gui::WidgetBuilder &builder) {
        return buildHomeQueue(builder, {}, model, svg);
    });
    result.recentGenerations = ii::gui::compiled_view_source([model = std::move(parts.recentGenerations), images = std::move(generationImages)](ii::gui::WidgetBuilder &builder) {
        return buildHomeRecentGenerations(builder, {}, model, images);
    });
    return result;
}
} // namespace dreamscapes::home
