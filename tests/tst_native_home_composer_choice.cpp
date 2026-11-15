#include "App/Views/Home/NativeHomeComposerChoice.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
void exercise() {
    auto composer = HomeComposerModel::create();
    auto picker = HomeComposerChoiceModel::create(composer, HomeComposerChoiceRole::model, "Model", "Choose model");
    check(!picker->enabled.get() && picker->value.get() == "Choose model", "empty model picker invented a selection");
    HomeComposerSnapshot state;
    state.modelId = "a"; state.modelLabel = "Model A";
    state.choices.models = {{"a", "Model A"}, {"b", "Model B"}, {"offline", "Unavailable", false}};
    state.choices.ratios = {{"1:1", "Square"}, {"16:9", "Wide"}}; state.aspectRatio = "1:1";
    state.choices.resolutions = {{"512", "512 × 512"}}; state.resolutionLabel = "512 × 512";
    good(composer->setSnapshot(state));
    int commands = 0; HomeComposerCommand last;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        ++commands; last = command; return {};
    }));
    check(picker->enabled.get() && picker->value.get() == "Model A" && picker->choices.get().size() == 3,
          "model inventory did not reach the choice view");
    good(picker->choose("b"));
    check(commands == 1 && last.kind == HomeComposerCommandKind::chooseModel && last.value == "b" &&
          picker->value.get() == "Model A", "selection changed without controller confirmation");
    auto updated = composer->snapshot.get(); updated.modelId = "b"; updated.modelLabel = "Model B";
    updated.choices.models = {{"b", "Model B"}, {"a", "Model A"}}; good(composer->setSnapshot(updated));
    check(picker->value.get() == "Model B" && !picker->choose("offline") && commands == 1,
          "reordered or removed options used a stale index");
    auto ratio = HomeComposerChoiceModel::create(composer, HomeComposerChoiceRole::ratio, "Aspect ratio", "Choose");
    good(ratio->choose("16:9")); check(last.kind == HomeComposerCommandKind::chooseRatio && last.value == "16:9", "ratio command was misrouted");
    auto resolution = HomeComposerChoiceModel::create(composer, HomeComposerChoiceRole::resolution, "Resolution", "Choose");
    check(resolution->enabled.get(), "image resolution is unavailable");
    updated = composer->snapshot.get();
    updated.choices.media = {{"Image", "Image"}, {"Video", "Video"}}; updated.mediaType = "Image";
    updated.choices.counts = {{"1", "1 image"}, {"4", "4 images"}}; updated.outputCount = 4;
    updated.choices.durations = {{"3", "3 s"}}; updated.videoDuration = 3;
    updated.choices.frameRates = {{"24", "24 FPS"}}; updated.videoFps = 24;
    updated.choices.presets = {{"Studio", "Studio"}}; updated.presetName = "Studio";
    good(composer->setSnapshot(updated));
    auto media = HomeComposerChoiceModel::create(composer, HomeComposerChoiceRole::media, "Media", "Choose", false);
    auto count = HomeComposerChoiceModel::create(composer, HomeComposerChoiceRole::count, "Count", "Choose");
    auto duration = HomeComposerChoiceModel::create(composer, HomeComposerChoiceRole::duration, "Duration", "Choose");
    auto fps = HomeComposerChoiceModel::create(composer, HomeComposerChoiceRole::frameRate, "Frame rate", "Choose");
    auto preset = HomeComposerChoiceModel::create(composer, HomeComposerChoiceRole::preset, "Preset", "Choose");
    check(!media->showHeading && count->value.get() == "4 images" && duration->value.get() == "3 s" &&
          fps->value.get() == "24 FPS" && preset->value.get() == "Studio", "choice labels were not supplied by the host");
    good(media->choose("Video")); check(last.kind == HomeComposerCommandKind::chooseMedia, "media choice was misrouted");
    good(count->choose("1")); check(last.kind == HomeComposerCommandKind::chooseCount, "count choice was misrouted");
    good(duration->choose("3")); check(last.kind == HomeComposerCommandKind::chooseDuration, "duration choice was misrouted");
    good(fps->choose("24")); check(last.kind == HomeComposerCommandKind::chooseFrameRate, "frame-rate choice was misrouted");
    good(preset->choose("Studio")); check(last.kind == HomeComposerCommandKind::loadPreset, "preset choice was misrouted");
    updated = composer->snapshot.get(); updated.video = true; good(composer->setSnapshot(updated));
    check(!resolution->enabled.get() && !resolution->choose("512"), "video resolution was editable from Home");
    good(composer->setHandler({})); check(!picker->enabled.get() && !ratio->enabled.get(), "detached controller left menus active");
    picker->close(); ratio->close(); resolution->close(); media->close(); count->close(); duration->close(); fps->close(); preset->close(); composer->close();

    auto reentrant = HomeComposerModel::create();
    HomeComposerSnapshot initial; initial.modelId = "a"; initial.modelLabel = "First";
    initial.choices.models = {{"a", "First"}}; good(reentrant->setSnapshot(initial));
    auto choice = HomeComposerChoiceModel::create(reentrant, HomeComposerChoiceRole::model, "Model", "Choose model");
    auto observation = choice->enabled.observe([&](bool active, std::uint64_t) {
        if (!active) return;
        auto next = reentrant->snapshot.get(); next.modelLabel = "New label";
        next.choices.models[0].label = "New label"; good(reentrant->setSnapshot(std::move(next)));
    });
    good(reentrant->setHandler([](const HomeComposerCommand &) -> ii::core::Result<void> { return {}; }));
    check(choice->value.get() == "New label" && choice->choices.get()[0].label == "New label",
          "reentrant controller update was overwritten by an older projection");
    observation.reset(); choice->close(); reentrant->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
