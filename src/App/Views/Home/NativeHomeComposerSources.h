#pragma once
#include "NativeHomeComposerPanel.h"
#include "NativeHomeComposerAction.h"
#include "NativeHomeComposerChoice.h"
#include "NativeHomeComposerSeed.h"
#include "NativeHomeNegativePrompt.h"
#include "NativeHomePresetSave.h"
#include "NativeHomeAttachments.h"
#include <LVUI/NativeImageResolver.h>
#include <iiGUI/Image.h>

namespace dreamscapes::home {
struct HomeComposerComponents final {
    std::shared_ptr<HomeComposerModel> composer;
    std::shared_ptr<HomeComposerPromptModel> prompt;
    std::shared_ptr<HomeNegativePromptModel> negative;
    std::shared_ptr<HomeComposerChoiceModel> media, model, ratio, resolution, count, duration, frameRate, preset;
    std::shared_ptr<HomeComposerSeedModel> seed;
    std::shared_ptr<HomePresetSaveModel> savePreset;
    std::shared_ptr<HomeAttachmentsModel> attachments;
    std::shared_ptr<HomeComposerActionModel> attach, paint, advanced, generate;
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images;
    std::string presetHeading;
};
// Each source retains its native view model. No placeholder control is substituted.
[[nodiscard]] HomeComposerPanelSources homeComposerPanelSources(HomeComposerComponents);
} // namespace dreamscapes::home
