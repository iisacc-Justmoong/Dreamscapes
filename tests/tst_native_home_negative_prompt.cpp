#include "App/Views/Home/NativeHomeNegativePrompt.h"
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
    auto composer = HomeComposerModel::create(); int edits = 0;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        check(command.kind == HomeComposerCommandKind::editNegativePrompt, "negative popup sent another command");
        ++edits; auto snapshot = composer->snapshot.get(); snapshot.negativePrompt = command.value;
        return composer->setSnapshot(std::move(snapshot));
    }));
    auto negative = HomeComposerPromptModel::create(composer, HomeComposerTextRole::negativePrompt, "", "Negative prompt", "", false);
    auto model = HomeNegativePromptModel::create(negative, "Negative prompt", "Done");
    good(model->setViewport(120, 22)); check(model->viewportWidth.get() == 120, "negative button geometry was lost");
    check(!model->setViewport(std::numeric_limits<double>::quiet_NaN(), 22), "invalid negative button geometry was accepted");
    good(negative->edit("blur\nnoise")); check(composer->snapshot.get().negativePrompt == "blur\nnoise" && edits == 1,
                                             "negative draft did not reach its own controller field");
    bool rejected = false;
    auto mainPrompt = HomeComposerPromptModel::create(composer, HomeComposerTextRole::prompt, "", "Prompt", "", false);
    try { (void)HomeNegativePromptModel::create(mainPrompt, "Negative prompt", "Done"); }
    catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "main prompt was accepted as a negative prompt");
    model->close(); check(!model->setViewport(10, 10) && negative->active(), "popup closure retired its externally owned draft");
    negative->close(); mainPrompt->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
