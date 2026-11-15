#include "App/Views/Home/NativeHomeComposerAction.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
void exercise() {
    auto composer = HomeComposerModel::create(); int commands = 0; HomeComposerCommandKind last{};
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        ++commands; last = command.kind; return {};
    }));
    auto attach = HomeComposerActionModel::create(composer, HomeComposerActionRole::attach, {"", "", "Attach reference images", {}});
    auto paint = HomeComposerActionModel::create(composer, HomeComposerActionRole::paint, {"Paint canvas", "", "Paint canvas", {}});
    auto advanced = HomeComposerActionModel::create(composer, HomeComposerActionRole::advanced, {"Advanced parameters", "More controls", "Advanced parameters", {}});
    auto generate = HomeComposerActionModel::create(composer, HomeComposerActionRole::generate,
        {"", "", "Generate", [](int count, bool video) { return std::to_string(count) + (video ? " videos" : " images"); }});
    check(attach->icon && !attach->primary && advanced->showHeading && generate->primary && generate->enabled.get() && commands == 0,
          "action types or initial state are wrong");
    good(attach->invoke()); check(last == HomeComposerCommandKind::chooseAttachments, "attach used the wrong command");
    good(paint->invoke()); check(last == HomeComposerCommandKind::paint, "paint used the wrong command");
    good(advanced->invoke()); check(last == HomeComposerCommandKind::advanced, "advanced used the wrong command");
    auto next = composer->snapshot.get(); next.video = true; next.outputCount = 3; good(composer->setSnapshot(next));
    check(generate->label.get() == "3 videos" && commands == 3, "generation label or projection is wrong");
    good(generate->invoke()); check(last == HomeComposerCommandKind::submit && commands == 4, "generate bypassed its command");
    next.generationPending = true; good(composer->setSnapshot(next));
    check(!generate->enabled.get() && !generate->invoke() && commands == 4 && attach->enabled.get(), "busy generation allowed duplicate submission");
    good(composer->setHandler({})); check(!attach->enabled.get() && !attach->invoke(), "detached actions remained active");
    attach->close(); paint->close(); advanced->close(); generate->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
