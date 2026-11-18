#include "NativeHomeWorkspace.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
namespace {
bool validDimension(double value) { return std::isfinite(value) && value >= 0 && value <= 8192; }
}
HomeWorkspaceModel::HomeWorkspaceModel(HomeWorkspaceSources views, std::shared_ptr<HomeSidebarModel> sidebar,
                                       HomeWorkspaceLabels strings, HomeWorkspaceActions actions)
    : sources(std::move(views)), labels(std::move(strings)), sidebar_(std::move(sidebar)), actions_(std::move(actions)) {
    if (!sources.sidebar || !sources.composer || !sources.recentFiles || !sources.selectedFile || !sources.queue ||
        !sources.recentGenerations || !sidebar_ || !sidebar_->active())
        throw std::invalid_argument("Home workspace requires all native sources and an active sidebar");
    (void)canOpenFile.set(bool(actions_.openFile)); (void)canNewCanvas.set(bool(actions_.newCanvas));
}
std::shared_ptr<HomeWorkspaceModel> HomeWorkspaceModel::create(HomeWorkspaceSources views,
        std::shared_ptr<HomeSidebarModel> sidebar, HomeWorkspaceLabels strings, HomeWorkspaceActions actions) {
    return std::shared_ptr<HomeWorkspaceModel>(new HomeWorkspaceModel(std::move(views), std::move(sidebar),
                                                                       std::move(strings), std::move(actions)));
}
HomeWorkspaceModel::~HomeWorkspaceModel() { if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {} }
ii::core::Result<void> HomeWorkspaceModel::writable() const {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Home workspace requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed, "Home workspace is closed");
    if (updating_) return ii::core::fail(ErrorCode::busy, "Home workspace update is in progress");
    return {};
}
ii::core::Result<void> HomeWorkspaceModel::setViewport(double width, double height, double gap, double expandedInset) {
    if (auto valid = writable(); !valid) return valid;
    if (!validDimension(width) || !validDimension(height) || !validDimension(gap) || !validDimension(expandedInset) ||
        gap > 512 || expandedInset > 512)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid Home viewport or LVUI spacing");
    return publish(width, height, composerHeight.get(), gap, expandedInset);
}
ii::core::Result<void> HomeWorkspaceModel::setComposerHeight(double height) {
    if (!validDimension(height)) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid generation composer height");
    if (owner_ == std::this_thread::get_id() && active_ && updating_) { pendingComposerHeight_ = height; return {}; }
    if (auto valid = writable(); !valid) return valid;
    return publish(windowWidth.get(), windowHeight.get(), height, gap_, expandedInset_);
}
ii::core::Result<void> HomeWorkspaceModel::publish(double width, double height, double composer, double gap, double expandedInset) {
    const auto keepAlive = shared_from_this();
    struct Reset final { bool &flag; ~Reset() { flag = false; } } reset{updating_}; updating_ = true;
    try {
      for (unsigned round = 0; round < 64; ++round) {
        pendingComposerHeight_.reset();
        const bool isCompact = width < 700;
        // The composed Home frame includes both LVUI space8 insets in its 181-point sidebar.
        const double navWidth = isCompact ? 48 : 181;
        const double viewport = std::max(0.0, width - navWidth);
        const double margin = isCompact ? gap : expandedInset;
        const double content = std::max(0.0, viewport - 2 * margin);
        const bool isWide = viewport >= 1040;
        const double available = std::max(0.0, content - gap);
        const double files = isWide ? available * 700 / 1215 : content;
        const double selected = isWide ? available - files : content;
        const double queueSize = isWide ? available * 526 / 1215 : content;
        const double generations = isWide ? available - queueSize : content;
        const double panelHeight = isWide ? std::max(360.0, height - composer - 284) : 360;
        if (auto result = sidebar_->setCompact(isCompact); !result) return result;
        sidebar_->sizeChanged(navWidth, height);
        gap_ = gap; expandedInset_ = expandedInset;
        (void)windowWidth.set(width); (void)windowHeight.set(height); (void)composerHeight.set(composer);
        (void)sidebarWidth.set(navWidth); (void)viewportWidth.set(viewport); (void)contentWidth.set(content); (void)inset.set(margin);
        (void)fileWidth.set(files); (void)selectedWidth.set(selected); (void)queueWidth.set(queueSize); (void)generationWidth.set(generations);
        (void)fileHeight.set(panelHeight);
        (void)hasSubtitle.set(content > 500); (void)compact.set(isCompact); (void)wide.set(isWide);
        (void)filePairHeight.set(isWide ? panelHeight : panelHeight * 2 + gap);
        (void)sessionPairHeight.set(isWide ? 160 : 320 + gap);
        (void)geometry.set({isWide, content, files, selected, queueSize, generations, panelHeight, gap});
        if (!pendingComposerHeight_ || *pendingComposerHeight_ == composer) return {};
        composer = *pendingComposerHeight_;
      }
      return ii::core::fail(ii::core::ErrorCode::busy, "Home composer height did not settle");
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot update Home workspace layout", std::current_exception()); }
}
ii::core::Result<void> HomeWorkspaceModel::setStatus(std::string value) {
    if (auto valid = writable(); !valid) return valid;
    try { (void)status.set(std::move(value)); return {}; }
    catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot update Home status", std::current_exception()); }
}
ii::core::Result<void> HomeWorkspaceModel::setError(std::string value) {
    if (auto valid = writable(); !valid) return valid;
    try { const bool present = !value.empty(); (void)error.set(std::move(value)); (void)hasError.set(present); return {}; }
    catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot update Home error", std::current_exception()); }
}
ii::core::Result<void> HomeWorkspaceModel::invoke(const std::function<void()> &action) {
    if (auto valid = writable(); !valid) return valid;
    const auto keepAlive = shared_from_this();
    try { auto command = action; if (command) command(); return {}; }
    catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot deliver Home command", std::current_exception()); }
}
ii::core::Result<void> HomeWorkspaceModel::openFile() { return invoke(actions_.openFile); }
ii::core::Result<void> HomeWorkspaceModel::newCanvas() { return invoke(actions_.newCanvas); }
void HomeWorkspaceModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Home workspace requires its owner thread");
    if (updating_) throw std::logic_error("Home workspace update is in progress");
    active_ = false; actions_ = {}; (void)canOpenFile.set(false); (void)canNewCanvas.set(false);
}
} // namespace dreamscapes::home
