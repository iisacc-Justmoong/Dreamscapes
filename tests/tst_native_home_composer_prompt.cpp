#include "App/Views/Home/NativeHomeComposerPrompt.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
void exercise() {
    auto composer = HomeComposerModel::create();
    HomeComposerSnapshot snapshot; snapshot.prompt = "첫 번째\nsecond"; snapshot.negativePrompt = "blur";
    good(composer->setSnapshot(snapshot));
    int commands = 0; HomeComposerCommand last;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        ++commands; last = command; auto value = composer->snapshot.get();
        if (command.kind == HomeComposerCommandKind::editPrompt) value.prompt = command.value;
        if (command.kind == HomeComposerCommandKind::editNegativePrompt) value.negativePrompt = command.value;
        return composer->setSnapshot(std::move(value));
    }));
    auto prompt = HomeComposerPromptModel::create(composer, HomeComposerTextRole::prompt, "Describe what to create…", "Prompt", "Clear prompt", true);
    auto negative = HomeComposerPromptModel::create(composer, HomeComposerTextRole::negativePrompt, "", "Negative prompt", "", false);
    good(prompt->setViewport(400, 50, 8, 22));
    check(prompt->text.get() == "첫 번째\nsecond" && prompt->clearVisible.get() && prompt->enabled.get() && prompt->editorWidth.get() == 370,
          "prompt state or LVUI spacing was lost");
    good(prompt->edit("한글\n둘째 줄"));
    check(commands == 1 && last.kind == HomeComposerCommandKind::editPrompt && composer->snapshot.get().prompt == "한글\n둘째 줄",
          "plain-text input did not reach the authoritative draft");
    good(negative->edit("no blur\nno noise"));
    check(commands == 2 && composer->snapshot.get().negativePrompt == "no blur\nno noise" && !negative->clearVisible.get(),
          "negative prompt did not remain separate");
    good(prompt->clear());
    check(commands == 3 && composer->snapshot.get().prompt.empty() && !prompt->clearVisible.get() && prompt->editorWidth.get() == 400,
          "clear did not deliver an empty prompt and restore its width");
    auto replacement = composer->snapshot.get(); replacement.prompt = "external";
    good(composer->setSnapshot(replacement));
    check(prompt->text.get() == "external" && commands == 3, "external update created a user command");
    check(!negative->submit() && commands == 3, "negative prompt submitted a generation");
    good(prompt->setMeasuredHeight(80, 2, 22)); check(prompt->requestedHeight.get() == 84, "measured height ignored LVUI padding");
    good(prompt->setMeasuredHeight(300, 2, 22)); check(prompt->requestedHeight.get() == 160, "prompt exceeded the existing height limit");
    check(!prompt->setViewport(std::numeric_limits<double>::quiet_NaN(), 30, 8, 22) &&
          !prompt->setMeasuredHeight(-1, 2, 22), "invalid layout was accepted");
    bool rejected = false;
    try { (void)HomeComposerPromptModel::create(composer, HomeComposerTextRole::prompt, "", "", "", true); }
    catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "an inaccessible prompt was accepted");
    good(composer->setHandler({})); check(!prompt->enabled.get() && !negative->enabled.get(), "retired controller left prompt inputs enabled");
    prompt->close(); check(!prompt->edit("retired") && commands == 3, "retired prompt delivered an edit");
    negative->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
