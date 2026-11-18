#include "NativeHomeComposerFactory.h"
#include "NativeHomeComposerPanelView.h"
#include <iiGUI>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
std::shared_ptr<HomeComposerPanelModel> createHomeComposerPanel(std::shared_ptr<HomeComposerModel> composer,
        HomeComposerLabels labels, HomeComposerLayoutSpec spec, HomeComposerLayoutMetrics metrics,
        std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images) {
    if (!composer || !composer->active() || !labels.generationLabel || !labels.removeAttachment)
        throw std::invalid_argument("Home composer requires a live draft and localized formatting callbacks");
    const auto choice = [&](HomeComposerChoiceRole role, const std::string &heading, const std::string &placeholder, bool show = true) {
        return HomeComposerChoiceModel::create(composer, role, heading, placeholder, show);
    };
    const auto action = [&](HomeComposerActionRole role, const std::string &text, const std::string &heading = {}) {
        return HomeComposerActionModel::create(composer, role, {text, heading, text, {}});
    };
    auto prompt = HomeComposerPromptModel::create(composer, HomeComposerTextRole::prompt,
        labels.promptPlaceholder, labels.prompt, labels.clearPrompt, true);
    auto negative = HomeComposerPromptModel::create(composer, HomeComposerTextRole::negativePrompt,
        spec.compactControls ? labels.negativePrompt : "", labels.negativePrompt, "", false);
    HomeComposerComponents parts;
    parts.composer = composer; parts.prompt = prompt;
    parts.negative = HomeNegativePromptModel::create(std::move(negative), labels.negativePrompt, labels.done);
    parts.media = choice(HomeComposerChoiceRole::media, labels.media, labels.media, false);
    parts.model = choice(HomeComposerChoiceRole::model, labels.model, labels.chooseModel, !spec.compactControls);
    parts.ratio = choice(HomeComposerChoiceRole::ratio, labels.ratio, labels.ratio, !spec.compactControls);
    parts.resolution = choice(HomeComposerChoiceRole::resolution, labels.resolution, labels.resolution, !spec.compactControls);
    parts.count = choice(HomeComposerChoiceRole::count, labels.count, labels.count, !spec.compactControls);
    parts.duration = choice(HomeComposerChoiceRole::duration, labels.duration, labels.duration, !spec.compactControls);
    parts.frameRate = choice(HomeComposerChoiceRole::frameRate, labels.frameRate, labels.frameRate, !spec.compactControls);
    parts.preset = choice(HomeComposerChoiceRole::preset, labels.loadPreset, labels.loadPreset, false);
    parts.seed = HomeComposerSeedModel::create(composer, labels.seed, labels.random, labels.apply, !spec.compactControls);
    parts.savePreset = HomePresetSaveModel::create(composer, labels.savePreset, labels.name, labels.save, labels.cancel);
    parts.attachments = HomeAttachmentsModel::create(composer, std::move(labels.removeAttachment));
    parts.attach = HomeComposerActionModel::create(composer, HomeComposerActionRole::attach, {"", "", labels.attach, {}});
    parts.paint = action(HomeComposerActionRole::paint, labels.paint);
    parts.advanced = action(HomeComposerActionRole::advanced, labels.advanced, labels.moreControls);
    parts.generate = HomeComposerActionModel::create(composer, HomeComposerActionRole::generate,
        {"", "", labels.generate, std::move(labels.generationLabel)}, spec.compactControls);
    parts.images = std::move(images); parts.presetHeading = labels.preset;
    return HomeComposerPanelModel::create(std::move(composer), std::move(prompt), homeComposerPanelSources(std::move(parts)), spec, metrics);
}
std::shared_ptr<ii::gui::NativeViewSource> homeComposerPanelSource(std::shared_ptr<HomeComposerPanelModel> panel) {
    if (!panel || !panel->active()) throw std::invalid_argument("Home composer source requires an active panel");
    return ii::gui::compiled_view_source([panel = std::move(panel)](ii::gui::WidgetBuilder &builder) {
        return buildHomeComposerPanel(builder, {}, panel);
    });
}
} // namespace dreamscapes::home
