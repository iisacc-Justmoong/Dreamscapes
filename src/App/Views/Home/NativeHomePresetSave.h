#pragma once
#include "NativeHomeComposer.h"
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::home {
class HomePresetSaveModel final : public std::enable_shared_from_this<HomePresetSaveModel> {
  public:
    static std::shared_ptr<HomePresetSaveModel> create(std::shared_ptr<HomeComposerModel>, std::string heading,
        std::string placeholder, std::string saveLabel, std::string cancelLabel);
    ~HomePresetSaveModel();
    HomePresetSaveModel(const HomePresetSaveModel &) = delete;
    HomePresetSaveModel &operator=(const HomePresetSaveModel &) = delete;
    [[nodiscard]] ii::core::Result<void> edit(std::string);
    [[nodiscard]] ii::core::Result<void> save();
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool isFor(const std::shared_ptr<HomeComposerModel> &owner) const noexcept { return active_ && composer_ == owner; }
    const std::string heading, placeholder, saveLabel, cancelLabel;
    ii::core::Observable<std::string> draft;
    ii::core::Observable<bool> enabled{false}, canSave{false};
  private:
    HomePresetSaveModel(std::shared_ptr<HomeComposerModel>, std::string, std::string, std::string, std::string);
    void start();
    void synchronize();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<HomeComposerModel> composer_;
    ii::core::ObservableSubscription commandsObservation_;
    bool active_ = true;
};
} // namespace dreamscapes::home
