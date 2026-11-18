#include "App/Views/Home/NativeHomeComposerSeedView.h"
#include "iiGUI/WidgetState.h"
#include <iiSDKCore>
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
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view id) {
    for (const auto &node : take(state->snapshots()))
        if (node.id == id || node.id.ends_with(std::string("/") + std::string(id))) return take(state->find(node.id));
    throw std::runtime_error("Missing seed widget " + std::string(id));
}
ii::gui::Widget input(const std::shared_ptr<ii::gui::detail::WidgetState> &state) {
    for (const auto &node : take(state->snapshots())) if (node.text_input) return take(state->find(node.id));
    throw std::runtime_error("Missing seed input");
}
void exercise() {
    using namespace ii::gui;
    auto composer = HomeComposerModel::create(); HomeComposerSnapshot snapshot;
    snapshot.seed = -1; snapshot.seedLabel = "Random"; good(composer->setSnapshot(snapshot));
    int attempts = 0;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        ++attempts; auto next = composer->snapshot.get();
        if (command.kind == HomeComposerCommandKind::applySeed) {
            if (command.value != "42") return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid seed");
            next.seed = 42; next.seedLabel = "42";
        } else if (command.kind == HomeComposerCommandKind::randomSeed) {
            next.seed = -1; next.seedLabel = "Random";
        } else throw std::runtime_error("unexpected seed action");
        return composer->setSnapshot(std::move(next));
    }));
    auto model = HomeComposerSeedModel::create(composer, "Seed", "Random", "Apply");
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {400,400}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerSeed(builder, {}, model); }));
    (void)take(state->drawing_snapshot({400,400}));
    check(take(find(state,"homeSeedButton").snapshot()).text == "Random", "seed label was not displayed");
    good(find(state,"homeSeedButton").activate());
    check(take(find(state,"homeSeedPopover").snapshot()).opened && model->draft.get().empty(), "seed editor did not open");
    good(input(state).focus()); good(state->input({.kind=WidgetInputKind::text,.text="invalid"}));
    good(find(state,"homeSeedApply").activate());
    check(attempts == 1 && model->draft.get() == "invalid" && take(find(state,"homeSeedPopover").snapshot()).opened,
          "invalid seed closed its editor or lost the draft");
    good(input(state).focus()); good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::select_all}));
    good(state->input({.kind=WidgetInputKind::text,.text="42"})); good(find(state,"homeSeedApply").activate());
    check(attempts == 2 && composer->snapshot.get().seed == 42 && !take(find(state,"homeSeedPopover").snapshot()).opened &&
          take(find(state,"homeSeedButton").snapshot()).text == "42", "applying the seed failed");
    good(find(state,"homeSeedButton").activate()); check(model->draft.get() == "42", "opening did not refresh the seed draft");
    good(find(state,"homeSeedRandom").activate());
    check(attempts == 3 && composer->snapshot.get().seed == -1 && !take(find(state,"homeSeedPopover").snapshot()).opened,
          "random action did not update the controller and close the popover");
    good(composer->setHandler({})); check(!take(find(state,"homeSeedButton").snapshot()).enabled, "detached seed remained active");
    state->close(); model->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
