#pragma once
#include "NativeHomeWorkspace.h"
#include "NativeHomeQueue.h"
#include "NativeHomeRecentFilesView.h"
#include "NativeHomeRecentGenerationsView.h"
#include "NativeHomeSelectedFileView.h"
#include <LVUI>
#include <iiGUI>

namespace dreamscapes::home {
struct HomeWorkspaceComponents final {
    std::shared_ptr<HomeSidebarModel> sidebar;
    std::shared_ptr<ii::gui::NativeViewSource> composer;
    std::shared_ptr<HomeRecentFilesModel> recentFiles;
    std::shared_ptr<HomeSelectedFileModel> selectedFile;
    std::shared_ptr<HomeQueueModel> queue;
    std::shared_ptr<HomeRecentGenerationsModel> recentGenerations;
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images;
    std::string queueReadySvg;
    // Session-specific preview identifiers may require different resolvers.
    // When absent, the corresponding view uses the shared image resolver.
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> recentFileImages;
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> recentGenerationImages;
};
// Each source retains its live model; images are resolved on demand by the caller's service.
[[nodiscard]] HomeWorkspaceSources homeWorkspaceSources(HomeWorkspaceComponents);
}
