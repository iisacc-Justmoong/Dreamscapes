#pragma once
#include "NativeHomeComposer.h"
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace dreamscapes::home {
struct HomeAttachmentRow final {
    std::string key, name, preview, detail, removeAccessible;
    bool operator==(const HomeAttachmentRow &) const = default;
};
class HomeAttachmentsModel final : public std::enable_shared_from_this<HomeAttachmentsModel> {
  public:
    using RemoveLabel = std::function<std::string(const std::string &)>;
    static std::shared_ptr<HomeAttachmentsModel> create(std::shared_ptr<HomeComposerModel>, RemoveLabel);
    ~HomeAttachmentsModel();
    HomeAttachmentsModel(const HomeAttachmentsModel &) = delete;
    HomeAttachmentsModel &operator=(const HomeAttachmentsModel &) = delete;
    [[nodiscard]] ii::core::Result<void> remove(std::string key);
    [[nodiscard]] ii::core::Result<void> setViewport(double width, double height);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool isFor(const std::shared_ptr<HomeComposerModel> &owner) const noexcept { return active_ && composer_ == owner; }
    ii::core::Observable<std::vector<HomeAttachmentRow>> rows;
    ii::core::Observable<bool> enabled{false}, visible{false};
    ii::core::Observable<double> viewportWidth{1}, viewportHeight{1};
  private:
    HomeAttachmentsModel(std::shared_ptr<HomeComposerModel>, RemoveLabel);
    void start();
    void synchronize();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<HomeComposerModel> composer_;
    RemoveLabel removeLabel_;
    ii::core::ObservableSubscription snapshotObservation_, commandsObservation_;
    bool active_ = true, publishing_ = false, pending_ = false;
};
} // namespace dreamscapes::home
