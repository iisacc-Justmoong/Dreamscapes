#include "NativeHomeComposerSources.h"
#include "NativeHomeComposerActionView.h"
#include "NativeHomeComposerChoiceView.h"
#include "NativeHomeComposerPromptView.h"
#include "NativeHomeComposerSeedView.h"
#include "NativeHomeNegativePromptView.h"
#include "NativeHomePresetGroupView.h"
#include "NativeHomePresetSaveView.h"
#include "NativeHomeAttachmentsView.h"
#include <iiGUI>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
namespace {
template<class Model, class Build, class... Args>
std::shared_ptr<ii::gui::NativeViewSource> source(std::shared_ptr<Model> model, Build build, Args... args) {
    return ii::gui::compiled_view_source([model = std::move(model), build, ...args = std::move(args)](ii::gui::WidgetBuilder &builder) {
        return build(builder, {}, model, args...);
    });
}
template<class Model> bool ready(const std::shared_ptr<Model> &model) { return model && model->active(); }
}
HomeComposerPanelSources homeComposerPanelSources(HomeComposerComponents parts) {
    if (!ready(parts.composer) || !ready(parts.prompt) || !ready(parts.negative) || !ready(parts.media) || !ready(parts.model) || !ready(parts.ratio) ||
        !ready(parts.resolution) || !ready(parts.count) || !ready(parts.duration) || !ready(parts.frameRate) || !ready(parts.preset) ||
        !ready(parts.seed) || !ready(parts.savePreset) || !ready(parts.attachments) || !ready(parts.attach) || !ready(parts.paint) ||
        !ready(parts.advanced) || !ready(parts.generate) || parts.presetHeading.empty())
        throw std::invalid_argument("Home composer components require every active view model and a preset label");
    using Choice = HomeComposerChoiceRole; using Action = HomeComposerActionRole;
    const auto &owner = parts.composer;
    const auto choiceFor = [&](const auto &model, Choice role) { return model->isFor(owner) && model->role() == role; };
    const auto actionFor = [&](const auto &model, Action role) { return model->isFor(owner) && model->role() == role; };
    if (!parts.prompt->isFor(owner) || !parts.prompt->supportsSubmission() || !parts.negative->prompt() ||
        !parts.negative->prompt()->isFor(owner) || parts.negative->prompt()->supportsSubmission() ||
        !choiceFor(parts.media,Choice::media) || !choiceFor(parts.model,Choice::model) || !choiceFor(parts.ratio,Choice::ratio) ||
        !choiceFor(parts.resolution,Choice::resolution) || !choiceFor(parts.count,Choice::count) || !choiceFor(parts.duration,Choice::duration) ||
        !choiceFor(parts.frameRate,Choice::frameRate) || !choiceFor(parts.preset,Choice::preset) ||
        !parts.seed->isFor(owner) || !parts.savePreset->isFor(owner) || !parts.attachments->isFor(owner) ||
        !actionFor(parts.attach,Action::attach) || !actionFor(parts.paint,Action::paint) ||
        !actionFor(parts.advanced,Action::advanced) || !actionFor(parts.generate,Action::generate))
        throw std::invalid_argument("Home composer controls must match their roles and the same generation draft");
    auto popups = std::make_shared<HomeComposerPopups>();
    const auto choice = [&](auto model) { return source(std::move(model), buildHomeComposerChoice, popups); };
    const auto action = [](auto model) { return source(std::move(model), buildHomeComposerAction); };
    HomeComposerPanelSources result;
    const auto negative = parts.negative->prompt();
    result.inlineNegative = source(negative, buildHomeComposerPrompt);
    result.inlineNegativeHeight = {negative, &negative->requestedHeight};
    using LabelValue=ii::core::Observable<std::string>;
    result.quickLabels = {std::shared_ptr<LabelValue>(parts.model,&parts.model->value),
        std::shared_ptr<LabelValue>(parts.ratio,&parts.ratio->value),std::shared_ptr<LabelValue>(parts.resolution,&parts.resolution->value),
        std::shared_ptr<LabelValue>(parts.count,&parts.count->value),std::shared_ptr<LabelValue>(parts.seed,&parts.seed->value),
        std::shared_ptr<LabelValue>(parts.duration,&parts.duration->value),
        std::shared_ptr<LabelValue>(parts.frameRate,&parts.frameRate->value)};
    auto group = HomePresetGroupModel::create(parts.presetHeading, choice(std::move(parts.preset)),
                                               source(std::move(parts.savePreset), buildHomePresetSave, popups));
    result.top = {choice(std::move(parts.media)), source(std::move(parts.prompt), buildHomeComposerPrompt),
                  action(std::move(parts.attach)), action(std::move(parts.paint)),
                  source(std::move(parts.negative), buildHomeNegativePrompt, popups), action(std::move(parts.generate))};
    result.settings = {choice(std::move(parts.model)), choice(std::move(parts.ratio)), choice(std::move(parts.resolution)),
                       choice(std::move(parts.count)), source(std::move(parts.seed), buildHomeComposerSeed, popups),
                       source(std::move(group), buildHomePresetGroup), action(std::move(parts.advanced)),
                       choice(std::move(parts.duration)), choice(std::move(parts.frameRate))};
    result.attachments = ii::gui::compiled_view_source([model = std::move(parts.attachments), images = std::move(parts.images)]
            (ii::gui::WidgetBuilder &builder) { return buildHomeAttachments(builder, {}, model, images); });
    return result;
}
} // namespace dreamscapes::home
