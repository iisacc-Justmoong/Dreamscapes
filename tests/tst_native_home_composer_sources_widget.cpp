#include "App/Views/Home/NativeHomeComposerSources.h"
#include "App/Views/Home/NativeHomeComposerPanelView.h"
#include "iiGUI/WidgetState.h"
#include <iiGUI/CompiledViewSource.h>
#include <iiSDKCore/Runtime/FrameLoop.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
template<class T> T take(ii::core::Result<T> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
    return std::move(*value);
}
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view id, std::string_view text = {}) {
    for (const auto &node : take(state->snapshots()))
        if ((node.id == id || node.id.ends_with(std::string("/") + std::string(id))) && (text.empty() || node.text == text))
            return take(state->find(node.id));
    throw std::runtime_error("Missing composer control " + std::string(id) + " " + std::string(text));
}
ii::gui::Widget input(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view name) {
    for (const auto &node : take(state->snapshots())) if (node.text_input && node.accessibility_name == name) return take(state->find(node.id));
    throw std::runtime_error("Missing composer input " + std::string(name));
}
void exercise() {
    using namespace ii::gui;
    auto composer = HomeComposerModel::create(); HomeComposerSnapshot snapshot;
    snapshot.mediaType = "Image"; snapshot.modelId = "a"; snapshot.modelLabel = "Photo A"; snapshot.aspectRatio = "1:1";
    snapshot.shortSide = 512; snapshot.resolutionLabel = "512 × 512"; snapshot.outputCount = 1; snapshot.seed = -1; snapshot.seedLabel = "Random";
    snapshot.choices.media = {{"Image","Image"},{"Video","Video"}};
    snapshot.choices.models = {{"a","Photo A"},{"b","Photo B"}};
    snapshot.choices.ratios = {{"1:1","1:1"},{"16:9","16:9"}}; snapshot.choices.resolutions = {{"512","512 × 512"}};
    snapshot.choices.counts = {{"1","1 image"},{"4","4 images"}}; snapshot.choices.durations = {{"3","3 s"}};
    snapshot.choices.frameRates = {{"24","24 FPS"}}; snapshot.choices.presets = {{"Studio","Studio"}};
    good(composer->setSnapshot(snapshot));
    int attached = 0, painted = 0, advanced = 0, generated = 0, removed = 0;
    good(composer->setHandler([&](const HomeComposerCommand &cmd) -> ii::core::Result<void> {
        auto next = composer->snapshot.get();
        switch (cmd.kind) {
        case HomeComposerCommandKind::editPrompt: next.prompt = cmd.value; break;
        case HomeComposerCommandKind::editNegativePrompt: next.negativePrompt = cmd.value; break;
        case HomeComposerCommandKind::chooseModel: next.modelId = cmd.value; next.modelLabel = cmd.value == "b" ? "Photo B" : "Photo A"; break;
        case HomeComposerCommandKind::chooseMedia: next.mediaType = cmd.value; next.video = cmd.value == "Video"; break;
        case HomeComposerCommandKind::chooseAttachments: ++attached; break;
        case HomeComposerCommandKind::paint: ++painted; break;
        case HomeComposerCommandKind::advanced: ++advanced; break;
        case HomeComposerCommandKind::submit: ++generated; break;
        case HomeComposerCommandKind::removeAttachment: ++removed; std::erase_if(next.attachments, [&](const auto &item) { return item.key == cmd.value; }); break;
        default: break;
        }
        return composer->setSnapshot(std::move(next));
    }));
    auto prompt = HomeComposerPromptModel::create(composer, HomeComposerTextRole::prompt, "Describe", "Prompt", "Clear", true);
    auto negative = HomeComposerPromptModel::create(composer, HomeComposerTextRole::negativePrompt, "", "Negative prompt", "", false);
    auto choice = [&](HomeComposerChoiceRole role, const char *heading, bool title = true) {
        return HomeComposerChoiceModel::create(composer, role, heading, heading, title);
    };
    auto action = [&](HomeComposerActionRole role, const char *label, const char *heading = "") {
        return HomeComposerActionModel::create(composer, role, {label,heading,label,{}});
    };
    HomeComposerComponents parts;
    parts.composer = composer; parts.prompt = prompt; parts.negative = HomeNegativePromptModel::create(negative,"Negative prompt","Done");
    parts.media = choice(HomeComposerChoiceRole::media,"Media",false); parts.model = choice(HomeComposerChoiceRole::model,"Model");
    parts.ratio = choice(HomeComposerChoiceRole::ratio,"Aspect ratio"); parts.resolution = choice(HomeComposerChoiceRole::resolution,"Resolution");
    parts.count = choice(HomeComposerChoiceRole::count,"Batch size"); parts.duration = choice(HomeComposerChoiceRole::duration,"Duration");
    parts.frameRate = choice(HomeComposerChoiceRole::frameRate,"Frame rate"); parts.preset = choice(HomeComposerChoiceRole::preset,"Load preset",false);
    parts.seed = HomeComposerSeedModel::create(composer,"Seed","Random","Apply");
    parts.savePreset = HomePresetSaveModel::create(composer,"Save preset","Name","Save","Cancel");
    parts.attachments = HomeAttachmentsModel::create(composer,[](const auto &name) { return "Remove " + name; });
    parts.attach = HomeComposerActionModel::create(composer,HomeComposerActionRole::attach,{"","","Attach images",{}});
    parts.paint = action(HomeComposerActionRole::paint,"Paint canvas"); parts.advanced = action(HomeComposerActionRole::advanced,"Advanced parameters","More controls");
    parts.generate = HomeComposerActionModel::create(composer,HomeComposerActionRole::generate,
        {"","","Generate",[](int count,bool video) { return "Generate " + std::to_string(count) + (video ? " videos" : " images"); }});
    parts.presetHeading = "Preset";
    auto incomplete = parts; incomplete.seed.reset(); bool rejected = false;
    try { (void)homeComposerPanelSources(std::move(incomplete)); } catch (const std::invalid_argument &) { rejected = true; }
    check(rejected,"missing native composer control was accepted");
    auto mismatched = parts; mismatched.model = mismatched.ratio; rejected = false;
    try { (void)homeComposerPanelSources(std::move(mismatched)); } catch (const std::invalid_argument &) { rejected = true; }
    check(rejected,"wrong native composer control role was accepted");
    auto another = HomeComposerModel::create();
    mismatched = parts; mismatched.seed = HomeComposerSeedModel::create(another,"Seed","Random","Apply"); rejected = false;
    try { (void)homeComposerPanelSources(std::move(mismatched)); } catch (const std::invalid_argument &) { rejected = true; }
    check(rejected,"native composer accepted a control from a different generation draft");
    another->close();
    auto sources = homeComposerPanelSources(parts);
    const HomeComposerLayoutSpec spec{1000,600,400,{88,1,22,93,138,145},{327,88,144,96,120,228,152}};
    const HomeComposerLayoutMetrics metrics{8,12,22,37,44};
    auto panel = HomeComposerPanelModel::create(composer,prompt,std::move(sources),spec,metrics);
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({},"root",{1227,400}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerPanel(builder,{},panel); }));
    (void)take(state->drawing_snapshot({1227,400})); (void)take(state->snapshots());
    auto editor = input(state,"Prompt"); good(editor.focus()); good(state->input({.kind=WidgetInputKind::text,.text="장면"}));
    good(find(state,"homeChoiceButton","Photo A").activate()); good(find(state,"homeChoiceEntry","Photo B").activate());
    check(composer->snapshot.get().modelId == "b", "composed model menu did not select the real key");
    good(find(state,"homeActionIcon").activate()); good(find(state,"homeActionDefault","Paint canvas").activate());
    good(find(state,"homeActionDefault","Advanced parameters").activate()); good(find(state,"homeActionPrimary").activate());
    check(attached == 1 && painted == 1 && advanced == 1 && generated == 1, "composed actions did not dispatch");
    auto next = composer->snapshot.get(); next.attachments = {{"ref","one.png","","1 MB"}}; good(composer->setSnapshot(next));
    (void)take(state->drawing_snapshot({1227,400})); good(find(state,"homeAttachmentRemove").activate());
    check(removed == 1 && composer->snapshot.get().attachments.empty(), "composed attachment removal failed");
    good(find(state,"homeNegativeButton").activate()); good(input(state,"Negative prompt").focus());
    good(state->input({.kind=WidgetInputKind::text,.text="blur"})); good(find(state,"homeNegativeDone").activate());
    check(composer->snapshot.get().negativePrompt == "blur", "composed negative editor did not update the draft");
    good(editor.focus()); good(root.set_extent({360,900})); (void)take(state->drawing_snapshot({360,900})); (void)take(state->snapshots());
    check(take(editor.snapshot()).focused && composer->snapshot.get().prompt == "장면" && std::abs(panel->contentWidth.get()-336) < .01,
          "composed composer lost its editor during resize");
    state->close(); panel->close(); prompt->close(); negative->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
