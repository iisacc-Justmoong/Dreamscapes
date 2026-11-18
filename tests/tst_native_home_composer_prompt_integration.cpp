#include "App/Views/Home/NativeHomeComposerPromptView.h"
#include "App/Views/Home/NativeHomeComposerSeedView.h"
#include "App/Views/Home/NativeHomePresetSaveView.h"
#include "App/Views/Home/NativeHomeAttachmentsView.h"
#include "App/Views/Home/NativeHomeComposerActionView.h"
#include "App/Views/Home/NativeHomeNegativePromptView.h"
#include "App/Views/Home/NativeHomeComposerFactory.h"
#include "App/Views/Home/NativeHomeComposerSession.h"
#include "App/Generation/AdvancedImageParameters.h"
#include "App/Generation/GenerationComposerModel.h"
#include "iiGUI/WidgetState.h"
#include <iiSDKCore>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
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
ii::gui::Widget input(const std::shared_ptr<ii::gui::detail::WidgetState> &state) {
    for (const auto &node : take(state->snapshots())) if (node.text_input) return take(state->find(node.id));
    throw std::runtime_error("Missing native input");
}
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view id) {
    for (const auto &node : take(state->snapshots()))
        if (node.id == id || node.id.ends_with(std::string("/") + std::string(id))) return take(state->find(node.id));
    throw std::runtime_error("Missing native widget " + std::string(id));
}
ii::gui::Widget namedInput(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view name) {
    for (const auto &node : take(state->snapshots()))
        if (node.text_input && node.accessibility_name == name) return take(state->find(node.id));
    throw std::runtime_error("Missing native input " + std::string(name));
}
HomeComposerLabels composerLabels() {
    return {"Describe", "Prompt", "Clear", "Negative prompt", "Done", "Media", "Model", "Choose model",
            "Aspect ratio", "Resolution", "Batch size", "Duration", "Frame rate", "Preset", "Load preset",
            "Save preset", "Name", "Save", "Cancel", "Seed", "Random", "Apply", "Attach images", "Paint canvas",
            "Advanced parameters", "More controls", "Generate", [](int n, bool video) { return std::to_string(n)+(video?" videos":" images"); },
            [](const auto &name) { return "Remove " + name; }};
}
void exercise() {
    using namespace ii::gui;
    auto draft = std::make_unique<GenerationComposerModel>();
    int submitted = 0;
    QObject::connect(draft.get(), &GenerationComposerModel::generateRequested,
        [&](QString, QString, QString, int) { ++submitted; });
    NativeHomeComposerSession session(draft.get(), {}, "Random", {});
    auto prompt = HomeComposerPromptModel::create(session.model(), HomeComposerTextRole::prompt,
        "Describe what to create…", "Prompt", "Clear prompt", true);
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {400,60}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerPrompt(builder, {}, prompt); }));
    (void)take(state->drawing_snapshot({400,60})); auto editor = input(state); good(editor.focus());
    good(state->input({.kind=WidgetInputKind::text,.text="사용자 입력\nnext"}));
    check(draft->prompt() == QString::fromUtf8("사용자 입력\nnext") && session.model()->snapshot.get().prompt == "사용자 입력\nnext",
          "native text did not reach the existing generation draft");
    good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter,.meta=true}));
    good(state->input({.kind=WidgetInputKind::key_up,.key=WidgetKey::enter,.meta=true}));
    check(submitted == 1, "native shortcut did not reach the existing generation submission signal");
    draft->setPrompt(QString::fromUtf8("외부 변경\nupdated"));
    const auto length = take(editor.snapshot()).text_length;
    check(take(editor.text_content({0,length})).text == "외부 변경\nupdated" && take(editor.snapshot()).focused,
          "existing controller update did not reach the focused native editor");
    draft.reset();
    check(!take(editor.snapshot()).enabled && !session.model()->canSubmit.get(), "destroyed controller left the native editor active");
    state->close(); prompt->close();
}
void exerciseSeed() {
    using namespace ii::gui;
    GenerationComposerModel draft;
    NativeHomeComposerSession session(&draft, {}, "Random", {});
    auto seed = HomeComposerSeedModel::create(session.model(), "Seed", "Random", "Apply");
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {400,400}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerSeed(builder, {}, seed); }));
    (void)take(state->drawing_snapshot({400,400}));
    good(find(state,"homeSeedButton").activate()); good(input(state).focus());
    good(state->input({.kind=WidgetInputKind::text,.text="invalid"})); good(find(state,"homeSeedApply").activate());
    check(draft.seed() == -1 && !draft.errorText().isEmpty() && take(find(state,"homeSeedPopover").snapshot()).opened,
          "invalid native seed did not preserve the controller validation and editor");
    good(input(state).focus()); good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::select_all}));
    good(state->input({.kind=WidgetInputKind::text,.text="123"})); good(find(state,"homeSeedApply").activate());
    check(draft.seed() == 123 && draft.errorText().isEmpty() && !take(find(state,"homeSeedPopover").snapshot()).opened,
          "native seed did not reach the real controller");
    good(find(state,"homeSeedButton").activate()); good(find(state,"homeSeedRandom").activate());
    check(draft.seed() == -1 && session.model()->snapshot.get().seedLabel == "Random", "native random did not reset the real controller");
    state->close(); seed->close();
}
void exercisePreset() {
    using namespace ii::gui;
    QTemporaryDir temporary; check(temporary.isValid(), "isolated preset directory unavailable");
    AdvancedImageParameters parameters(temporary.filePath("presets.json"));
    GenerationComposerModel draft; draft.setParameterStore(&parameters); draft.setPrompt("draft prompt");
    NativeHomeComposerSession session(&draft, {}, "Random", {});
    auto preset = HomePresetSaveModel::create(session.model(), "Save preset", "Name", "Save", "Cancel");
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {400,400}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomePresetSave(builder, {}, preset); }));
    (void)take(state->drawing_snapshot({400,400}));
    good(find(state,"homeSavePreset").activate()); good(input(state).focus());
    good(state->input({.kind=WidgetInputKind::text,.text=std::string(101, 'x')})); good(find(state,"homePresetSave").activate());
    check(parameters.presets().isEmpty() && !draft.errorText().isEmpty() && take(find(state,"homePresetSavePopover").snapshot()).opened &&
          preset->draft.get().size() == 101, "native preset did not preserve store validation and draft");
    good(input(state).focus()); good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::select_all}));
    good(state->input({.kind=WidgetInputKind::text,.text="Studio"})); good(find(state,"homePresetSave").activate());
    check(parameters.presets() == QStringList{"Studio"} && draft.selectedPresetName() == "Studio" &&
          draft.errorText().isEmpty() && preset->draft.get().empty() && !take(find(state,"homePresetSavePopover").snapshot()).opened,
          "native preset did not reach the existing isolated parameter store");
    state->close(); preset->close();
}
void exerciseAttachments() {
    using namespace ii::gui;
    GenerationComposerModel draft;
    std::vector<HomeComposerAttachment> attachments{{"a", "one.png", "", "1 MB"}, {"b", "two.png", "", "2 MB"}};
    std::unique_ptr<NativeHomeComposerSession> session;
    NativeHomeComposerSession::Callbacks callbacks;
    callbacks.removeAttachment = [&](std::string key) -> ii::core::Result<void> {
        std::erase_if(attachments, [&](const auto &item) { return item.key == key; });
        return session->setAttachments(attachments);
    };
    session = std::make_unique<NativeHomeComposerSession>(&draft, HomeComposerChoices{}, "Random", std::move(callbacks));
    good(session->setAttachments(attachments));
    auto model = HomeAttachmentsModel::create(session->model(), [](const auto &name) { return "Remove " + name; });
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {400,44}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeAttachments(builder, {}, model); }));
    (void)take(state->drawing_snapshot({400,44}));
    auto list = find(state,"homeAttachments"); auto remove = take(list.keyed_content_item("b", "homeAttachmentRemove"));
    good(remove.activate());
    check(attachments.size() == 1 && attachments.front().key == "a" && session->model()->snapshot.get().attachments == attachments &&
          model->rows.get().size() == 1 && !remove.activate(), "native attachment action did not update the session by stable key");
    state->close(); model->close();
}
void exerciseActions() {
    using namespace ii::gui;
    GenerationComposerModel draft; draft.setPrompt("A landscape");
    int attached = 0, painted = 0, advanced = 0, submitted = 0;
    QObject::connect(&draft, &GenerationComposerModel::generateRequested,
        [&](QString, QString, QString, int) { ++submitted; });
    NativeHomeComposerSession::Callbacks callbacks;
    callbacks.chooseAttachments = [&]() -> ii::core::Result<void> { ++attached; return {}; };
    callbacks.paint = [&]() -> ii::core::Result<void> { ++painted; return {}; };
    callbacks.advanced = [&](QVariantMap params) -> ii::core::Result<void> {
        check(params.value("prompt").toString() == "A landscape", "advanced action lost the real draft"); ++advanced; return {};
    };
    NativeHomeComposerSession session(&draft, {}, "Random", std::move(callbacks));
    auto attach = HomeComposerActionModel::create(session.model(), HomeComposerActionRole::attach, {"", "", "Attach images", {}});
    auto paint = HomeComposerActionModel::create(session.model(), HomeComposerActionRole::paint, {"Paint canvas", "", "Paint canvas", {}});
    auto more = HomeComposerActionModel::create(session.model(), HomeComposerActionRole::advanced, {"Advanced", "More controls", "Advanced", {}});
    auto generate = HomeComposerActionModel::create(session.model(), HomeComposerActionRole::generate,
        {"", "", "Generate", [](int count, bool) { return "Generate " + std::to_string(count); }});
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {180,44}));
    const auto mount = [&](const auto &model, std::string_view name) {
        good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerAction(builder, {}, model); }));
        (void)take(state->drawing_snapshot({180,44})); good(find(state, name).activate());
    };
    mount(attach,"homeActionIcon"); mount(paint,"homeActionDefault"); mount(more,"homeActionDefault");
    mount(generate,"homeActionPrimary");
    check(attached == 1 && painted == 1 && advanced == 1 && submitted == 1,
          "native actions did not reach the existing controller and host callbacks");
    draft.setGenerationPending(true); (void)find(state,"homeActionPrimary").activate();
    check(submitted == 1 && !take(find(state,"homeActionPrimary").snapshot()).enabled, "pending generation was submitted twice");
    state->close(); attach->close(); paint->close(); more->close(); generate->close();
}
void exerciseNegative() {
    using namespace ii::gui;
    GenerationComposerModel draft; draft.setPrompt("positive"); int submitted = 0;
    QObject::connect(&draft, &GenerationComposerModel::generateRequested,
        [&](QString, QString, QString, int) { ++submitted; });
    NativeHomeComposerSession session(&draft, {}, "Random", {});
    auto prompt = HomeComposerPromptModel::create(session.model(), HomeComposerTextRole::negativePrompt, "", "Negative prompt", "", false);
    auto model = HomeNegativePromptModel::create(prompt, "Negative prompt", "Done");
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {500,300}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeNegativePrompt(builder, {}, model); }));
    (void)take(state->drawing_snapshot({500,300})); good(find(state,"homeNegativeButton").activate());
    good(input(state).focus()); good(state->input({.kind=WidgetInputKind::text,.text="blur\nnoise"}));
    good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter,.meta=true}));
    good(find(state,"homeNegativeDone").activate());
    check(draft.negativePrompt() == "blur\nnoise" && draft.prompt() == "positive" && submitted == 0,
          "native negative prompt changed the wrong field or submitted a generation");
    state->close(); model->close(); prompt->close();
}
void exerciseComposerFactory() {
    using namespace ii::gui;
    QTemporaryDir temporary; check(temporary.isValid(), "isolated composer directory unavailable");
    AdvancedImageParameters parameters(temporary.filePath("presets.json"));
    GenerationComposerModel draft; draft.setParameterStore(&parameters); int attached = 0, submitted = 0;
    QObject::connect(&draft, &GenerationComposerModel::generateRequested,
        [&](QString, QString, QString, int) { ++submitted; });
    HomeComposerChoices choices;
    choices.media = {{"Image","Image"},{"Video","Video"}}; choices.ratios = {{"1:1","1:1"}};
    choices.resolutions = {{"512","512 × 512"},{"1024","1024 × 1024"}}; choices.counts = {{"1","1 image"}};
    choices.durations = {{"3","3 s"}}; choices.frameRates = {{"24","24 FPS"}};
    NativeHomeComposerSession::Callbacks callbacks;
    callbacks.chooseAttachments = [&]() -> ii::core::Result<void> { ++attached; return {}; };
    NativeHomeComposerSession session(&draft,choices,"Random",std::move(callbacks));
    const HomeComposerLayoutSpec spec{1000,600,400,{88,1,22,93,138,145},{327,88,144,96,120,228,152}};
    const HomeComposerLayoutMetrics metrics{8,12,22,37,44};
    auto panel = createHomeComposerPanel(session.model(),composerLabels(),spec,metrics);
    auto source = homeComposerPanelSource(panel);
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({},"root",{1227,400}));
    good(root.set_content([&](WidgetBuilder &builder) { return source->build(builder); }));
    (void)take(state->drawing_snapshot({1227,400})); (void)take(state->snapshots());
    good(namedInput(state,"Prompt").focus()); good(state->input({.kind=WidgetInputKind::text,.text="실제 초안"}));
    good(find(state,"homeActionIcon").activate()); good(find(state,"homeActionPrimary").activate());
    check(draft.prompt() == QString::fromUtf8("실제 초안") && attached == 1 && submitted == 1,
          "constructed native composer did not reach the real draft and host actions");
    good(find(state,"homeSeedButton").activate()); good(namedInput(state,"Seed").focus());
    good(state->input({.kind=WidgetInputKind::text,.text="invalid"})); good(find(state,"homeSeedApply").activate());
    (void)take(state->drawing_snapshot({1227,400}));
    check(!draft.errorText().isEmpty() && panel->hasError.get() && take(find(state,"homeComposerErrorText").snapshot()).text == draft.errorText().toStdString(),
          "constructed native composer did not display the real controller error");
    good(find(state,"homeSeedRandom").activate()); check(draft.errorText().isEmpty(), "random did not clear the real error");
    draft.setGenerationPending(true); (void)find(state,"homeActionPrimary").activate();
    check(submitted == 1 && !take(find(state,"homeActionPrimary").snapshot()).enabled,
          "constructed native composer allowed duplicate submission");
    state->close(); panel->close();
}
}
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    try { exercise(); exerciseSeed(); exercisePreset(); exerciseAttachments(); exerciseActions(); exerciseNegative(); exerciseComposerFactory(); }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
