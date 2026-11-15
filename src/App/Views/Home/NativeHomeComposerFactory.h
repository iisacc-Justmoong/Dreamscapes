#pragma once
#include "NativeHomeComposerSources.h"
#include <functional>
#include <string>

namespace dreamscapes::home {
struct HomeComposerLabels final {
    std::string promptPlaceholder, prompt, clearPrompt, negativePrompt, done;
    std::string media, model, chooseModel, ratio, resolution, count, duration, frameRate;
    std::string preset, loadPreset, savePreset, name, save, cancel, seed, random, apply;
    std::string attach, paint, advanced, moreControls, generate;
    std::function<std::string(int, bool)> generationLabel;
    HomeAttachmentsModel::RemoveLabel removeAttachment;
};
// The returned panel and source retain their control models, not the app session.
[[nodiscard]] std::shared_ptr<HomeComposerPanelModel> createHomeComposerPanel(
    std::shared_ptr<HomeComposerModel>, HomeComposerLabels, HomeComposerLayoutSpec, HomeComposerLayoutMetrics,
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images = {});
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> homeComposerPanelSource(std::shared_ptr<HomeComposerPanelModel>);
} // namespace dreamscapes::home
