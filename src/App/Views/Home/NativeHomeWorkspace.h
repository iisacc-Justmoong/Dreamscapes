#pragma once

#include "NativeHomeSidebar.h"
#include <iiGUI>
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace dreamscapes::home {
struct HomeWorkspaceSources final {
    std::shared_ptr<ii::gui::NativeViewSource> sidebar, composer, recentFiles, selectedFile, queue, recentGenerations;
};
struct HomeWorkspaceLabels final { std::string heading, subtitle, openFile, newCanvas, storage; };
struct HomeWorkspaceActions final { std::function<void()> openFile, newCanvas; };
struct HomeWorkspaceGeometry final {
    bool wide = false;
    double content = 0, file = 0, selected = 0, queue = 0, generations = 0, fileHeight = 360, gap = 0;
    bool operator==(const HomeWorkspaceGeometry &) const = default;
};

// A projection of window dimensions and commands. It owns no file or generation state.
class HomeWorkspaceModel final : public std::enable_shared_from_this<HomeWorkspaceModel> {
  public:
    static std::shared_ptr<HomeWorkspaceModel> create(HomeWorkspaceSources, std::shared_ptr<HomeSidebarModel>,
                                                       HomeWorkspaceLabels, HomeWorkspaceActions);
    ~HomeWorkspaceModel();
    HomeWorkspaceModel(const HomeWorkspaceModel &) = delete;
    HomeWorkspaceModel &operator=(const HomeWorkspaceModel &) = delete;
    [[nodiscard]] ii::core::Result<void> setViewport(double width, double height, double gap, double expandedInset);
    [[nodiscard]] ii::core::Result<void> setComposerHeight(double height);
    [[nodiscard]] ii::core::Result<void> setStatus(std::string);
    [[nodiscard]] ii::core::Result<void> setError(std::string);
    [[nodiscard]] ii::core::Result<void> openFile();
    [[nodiscard]] ii::core::Result<void> newCanvas();
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    const HomeWorkspaceSources sources;
    const HomeWorkspaceLabels labels;
    ii::core::Observable<bool> compact{false}, wide{false}, hasSubtitle{false}, hasError{false}, canOpenFile{false}, canNewCanvas{false};
    ii::core::Observable<double> windowWidth{0}, windowHeight{0}, sidebarWidth{0}, viewportWidth{0},
        contentWidth{0}, inset{0}, fileWidth{0}, selectedWidth{0}, queueWidth{0}, generationWidth{0},
        fileHeight{360}, composerHeight{0}, filePairHeight{720}, sessionPairHeight{320};
    ii::core::Observable<std::string> status, error;
    ii::core::Observable<HomeWorkspaceGeometry> geometry;
  private:
    HomeWorkspaceModel(HomeWorkspaceSources, std::shared_ptr<HomeSidebarModel>, HomeWorkspaceLabels, HomeWorkspaceActions);
    [[nodiscard]] ii::core::Result<void> writable() const;
    [[nodiscard]] ii::core::Result<void> publish(double width, double height, double composer, double gap, double expandedInset);
    [[nodiscard]] ii::core::Result<void> invoke(const std::function<void()> &);
    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<HomeSidebarModel> sidebar_;
    HomeWorkspaceActions actions_;
    double gap_ = 0, expandedInset_ = 0;
    std::optional<double> pendingComposerHeight_;
    bool active_ = true, updating_ = false;
};
} // namespace dreamscapes::home
