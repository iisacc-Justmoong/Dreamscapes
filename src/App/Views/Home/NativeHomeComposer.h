#pragma once
#include <iiSDKCore/Core/Property/Observable.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace dreamscapes::home {
struct HomeComposerChoice final {
    std::string key, label;
    bool enabled = true;
    bool operator==(const HomeComposerChoice &) const = default;
};
struct HomeComposerAttachment final {
    std::string key, name, preview, detail;
    bool operator==(const HomeComposerAttachment &) const = default;
};
struct HomeComposerChoices final {
    std::vector<HomeComposerChoice> media, models, ratios, resolutions, counts, durations, frameRates, presets;
    bool operator==(const HomeComposerChoices &) const = default;
};
// All values are supplied by the real generation and file controllers.
struct HomeComposerSnapshot final {
    std::string prompt, negativePrompt, mediaType, modelId, modelLabel, aspectRatio, resolutionLabel,
        seedLabel, presetName, errorText;
    int outputCount = 0, videoDuration = 0, videoFps = 0, shortSide = 0, width = 0, height = 0;
    double seed = -1;
    bool video = false, submitting = false, generationPending = false;
    bool generationAvailable = true, attachmentsAvailable = true, paintAvailable = true, advancedAvailable = true;
    HomeComposerChoices choices;
    std::vector<HomeComposerAttachment> attachments;
    bool operator==(const HomeComposerSnapshot &) const = default;
};
enum class HomeComposerCommandKind {
    editPrompt, editNegativePrompt, chooseMedia, chooseModel, chooseRatio, chooseResolution,
    chooseCount, chooseDuration, chooseFrameRate, applySeed, randomSeed, loadPreset, savePreset,
    chooseAttachments, removeAttachment, paint, advanced, submit
};
struct HomeComposerCommand final {
    HomeComposerCommandKind kind = HomeComposerCommandKind::editPrompt;
    std::string value;
    bool operator==(const HomeComposerCommand &) const = default;
};
class HomeComposerModel final : public std::enable_shared_from_this<HomeComposerModel> {
  public:
    using Handler = std::function<ii::core::Result<void>(const HomeComposerCommand &)>;
    static std::shared_ptr<HomeComposerModel> create();
    ~HomeComposerModel();
    HomeComposerModel(const HomeComposerModel &) = delete;
    HomeComposerModel &operator=(const HomeComposerModel &) = delete;
    [[nodiscard]] ii::core::Result<void> setSnapshot(HomeComposerSnapshot);
    [[nodiscard]] ii::core::Result<void> setHandler(Handler);
    [[nodiscard]] ii::core::Result<void> dispatch(HomeComposerCommandKind, std::string value = {});
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    ii::core::Observable<HomeComposerSnapshot> snapshot;
    ii::core::Observable<bool> commandsAvailable{false}, canSubmit{false};
  private:
    HomeComposerModel() = default;
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_ = std::this_thread::get_id();
    Handler handler_;
    bool active_ = true, updating_ = false, dispatching_ = false;
};
} // namespace dreamscapes::home
