#include "App/Views/Home/NativeHomeComposer.h"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) {
    if (!result) { if (result.error().cause) std::rethrow_exception(result.error().cause); throw std::runtime_error(result.error().message); }
}
void exercise() {
    auto model = HomeComposerModel::create();
    HomeComposerSnapshot state;
    state.prompt = "한글 prompt\nsecond line"; state.negativePrompt = "blur"; state.mediaType = "Image";
    state.modelId = "model-a"; state.aspectRatio = "1:1"; state.outputCount = 4;
    state.choices.media = {{"Image", "Image"}, {"Video", "Video"}, {"Audio", "Audio"}, {"Board", "Board"}};
    state.choices.models = {{"model-a", "Model A"}, {"model-b", "Model B"}, {"offline", "Unavailable", false}};
    state.choices.ratios = {{"1:1", "1:1"}, {"16:9", "16:9"}};
    state.choices.counts = {{"1", "1 image"}, {"4", "4 images"}};
    state.choices.presets = {{"preset-1", "Studio"}};
    state.attachments = {{"attachment-a", "reference.png", "file:///reference.png", "1 MB"},
                         {"attachment-b", "other.png", "file:///other.png", "2 MB"}};
    good(model->setSnapshot(state));
    check(model->snapshot.get() == state && !model->canSubmit.get(), "snapshot changed or unbound generation was enabled");
    HomeComposerCommand last; int calls = 0;
    good(model->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        last = command; ++calls;
        if (command.kind == HomeComposerCommandKind::editPrompt) {
            auto update = model->snapshot.get(); update.prompt = command.value;
            return model->setSnapshot(std::move(update));
        }
        return {};
    }));
    check(model->canSubmit.get(), "generation did not become available with a command handler");
    good(model->dispatch(HomeComposerCommandKind::editPrompt, "새 프롬프트\n두 번째 줄"));
    check(model->snapshot.get().prompt == "새 프롬프트\n두 번째 줄" && calls == 1, "plain-text edit was not delivered");
    good(model->dispatch(HomeComposerCommandKind::chooseModel, "model-b"));
    check(last.value == "model-b" && calls == 2 && model->snapshot.get().modelId == "model-a",
          "model selection did not preserve authoritative state");
    check(!model->dispatch(HomeComposerCommandKind::chooseModel, "missing") &&
          !model->dispatch(HomeComposerCommandKind::chooseModel, "offline") && calls == 2,
          "unknown or disabled model reached the controller");
    auto reordered = model->snapshot.get();
    reordered.choices.models = {{"model-b", "Model B"}, {"model-a", "Model A"}};
    std::swap(reordered.attachments[0], reordered.attachments[1]);
    good(model->setSnapshot(reordered));
    good(model->dispatch(HomeComposerCommandKind::removeAttachment, "attachment-a"));
    check(last.kind == HomeComposerCommandKind::removeAttachment && last.value == "attachment-a", "attachment identity was lost after reordering");
    check(!model->dispatch(HomeComposerCommandKind::removeAttachment, "removed") &&
          !model->dispatch(HomeComposerCommandKind::savePreset, "  \n "), "stale attachment or empty preset was accepted");
    good(model->dispatch(HomeComposerCommandKind::chooseMedia, "Video"));
    good(model->dispatch(HomeComposerCommandKind::chooseRatio, "16:9"));
    good(model->dispatch(HomeComposerCommandKind::loadPreset, "preset-1"));
    good(model->dispatch(HomeComposerCommandKind::submit));
    auto busy = model->snapshot.get(); busy.generationPending = true; good(model->setSnapshot(busy));
    const int beforeBusy = calls;
    check(!model->canSubmit.get() && !model->dispatch(HomeComposerCommandKind::submit) && calls == beforeBusy,
          "busy submission reached the controller");
    auto bad = busy; bad.choices.models.push_back({"model-a", "Duplicate"});
    check(!model->setSnapshot(bad) && model->snapshot.get() == busy, "invalid snapshot replaced the live composer");
    bool wrongThread = false;
    std::thread thread([&] { wrongThread = !model->dispatch(HomeComposerCommandKind::paint); }); thread.join();
    check(wrongThread && calls == beforeBusy, "wrong-thread command was delivered");
    model->close();
    check(!model->dispatch(HomeComposerCommandKind::paint) && !model->canSubmit.get() && calls == beforeBusy,
          "retired composer delivered an action");
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
