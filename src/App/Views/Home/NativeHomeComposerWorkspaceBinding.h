#pragma once
#include "NativeHomeComposerPanel.h"
#include "NativeHomeWorkspace.h"
#include <functional>
#include <memory>
#include <thread>

namespace dreamscapes::home {
class HomeComposerWorkspaceBinding final : public std::enable_shared_from_this<HomeComposerWorkspaceBinding> {
  public:
    using Failure = std::function<void(ii::core::Error)>;
    static std::shared_ptr<HomeComposerWorkspaceBinding> create(std::shared_ptr<HomeComposerPanelModel>,
                                                                 std::shared_ptr<HomeWorkspaceModel>, Failure = {});
    ~HomeComposerWorkspaceBinding();
    HomeComposerWorkspaceBinding(const HomeComposerWorkspaceBinding &) = delete;
    HomeComposerWorkspaceBinding &operator=(const HomeComposerWorkspaceBinding &) = delete;
    void close();
  private:
    HomeComposerWorkspaceBinding(std::shared_ptr<HomeComposerPanelModel>, std::shared_ptr<HomeWorkspaceModel>, Failure);
    void start();
    void publish(double height);
    const std::thread::id owner_ = std::this_thread::get_id();
    std::weak_ptr<HomeComposerPanelModel> composer_;
    std::weak_ptr<HomeWorkspaceModel> workspace_;
    ii::core::ObservableSubscription observation_;
    Failure failure_;
    bool active_ = true;
};
} // namespace dreamscapes::home
