#include "NativeHomeComposerWorkspaceBinding.h"
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
HomeComposerWorkspaceBinding::HomeComposerWorkspaceBinding(std::shared_ptr<HomeComposerPanelModel> composer,
        std::shared_ptr<HomeWorkspaceModel> workspace, Failure failure)
    : composer_(std::move(composer)), workspace_(std::move(workspace)), failure_(std::move(failure)) {
    auto panel = composer_.lock(); auto home = workspace_.lock();
    if (!panel || !panel->active() || !home || !home->active())
        throw std::invalid_argument("Composer workspace binding requires active models");
}
std::shared_ptr<HomeComposerWorkspaceBinding> HomeComposerWorkspaceBinding::create(
        std::shared_ptr<HomeComposerPanelModel> composer, std::shared_ptr<HomeWorkspaceModel> workspace, Failure failure) {
    auto result = std::shared_ptr<HomeComposerWorkspaceBinding>(new HomeComposerWorkspaceBinding(
        std::move(composer), std::move(workspace), std::move(failure)));
    result->start(); return result;
}
HomeComposerWorkspaceBinding::~HomeComposerWorkspaceBinding() { if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {} }
void HomeComposerWorkspaceBinding::start() {
    const std::weak_ptr<HomeComposerWorkspaceBinding> weak = shared_from_this();
    auto panel = composer_.lock();
    observation_ = panel->requestedHeight.observe([weak](double height, std::uint64_t) {
        if (auto binding = weak.lock(); binding && binding->active_) binding->publish(height);
    });
    publish(panel->requestedHeight.get());
}
void HomeComposerWorkspaceBinding::publish(double height) {
    if (!active_) return;
    auto workspace = workspace_.lock();
    if (!workspace || !workspace->active()) return;
    auto result = workspace->setComposerHeight(height);
    if (!result && active_ && failure_) {
        auto report = failure_;
        try { report(result.error()); } catch (...) {}
    }
}
void HomeComposerWorkspaceBinding::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Composer workspace binding requires its owner thread");
    if (!active_) return;
    active_ = false; observation_.reset(); composer_.reset(); workspace_.reset(); failure_ = {};
}
} // namespace dreamscapes::home
