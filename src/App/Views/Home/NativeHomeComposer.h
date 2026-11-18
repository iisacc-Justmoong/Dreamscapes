#pragma once
#include <iiSDKCore>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace dreamscapes::home {
struct HomeComposerChoice final {
    std::string key, label;
    bool enabled = true;
    bool operator==(const HomeComposerChoice &) const = default;
};
// Draft snapshots share the immutable lists, including during synchronous
// observer delivery. A catalogue replacement gives existing readers a stable
// view of the previous entries until they release that snapshot.
class HomeComposerChoiceList final {
  public:
    HomeComposerChoiceList() = default;
    HomeComposerChoiceList(std::vector<HomeComposerChoice> entries)
        : entries_(entries.empty() ? nullptr : std::make_shared<const std::vector<HomeComposerChoice>>(std::move(entries))) {}
    HomeComposerChoiceList(std::initializer_list<HomeComposerChoice> entries)
        : HomeComposerChoiceList(std::vector<HomeComposerChoice>(entries)) {}

    [[nodiscard]] const std::vector<HomeComposerChoice> &entries() const noexcept {
        static const std::vector<HomeComposerChoice> empty;
        return entries_ ? *entries_ : empty;
    }
    auto begin() const noexcept { return entries().begin(); }
    auto end() const noexcept { return entries().end(); }
    auto data() const noexcept { return entries().data(); }
    auto size() const noexcept { return entries().size(); }
    bool empty() const noexcept { return entries().empty(); }
    const HomeComposerChoice &front() const noexcept { return entries().front(); }
    const HomeComposerChoice &operator[](std::size_t index) const noexcept { return entries()[index]; }
    bool operator==(const HomeComposerChoiceList &other) const {
        return entries_ == other.entries_ || entries() == other.entries();
    }
  private:
    std::shared_ptr<const std::vector<HomeComposerChoice>> entries_;
};
struct HomeComposerAttachment final {
    std::string key, name, preview, detail;
    bool operator==(const HomeComposerAttachment &) const = default;
};
struct HomeComposerChoices final {
    HomeComposerChoiceList media, models, ratios, resolutions, counts, durations, frameRates, presets;
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
