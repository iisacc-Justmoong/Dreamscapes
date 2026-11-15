#include "App/Views/Home/NativeHomeRecentGenerations.h"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace dreamscapes;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) {
    if (!result) { if (result.error().cause) std::rethrow_exception(result.error().cause); throw std::runtime_error(result.error().message); }
}
vm::Value file(const char *id, const char *name, const char *source, const char *type) {
    return vm::Value(vm::Object{{"id", vm::Value(id)}, {"name", vm::Value(name)}, {"source", vm::Value(source)},
        {"mediaType", vm::Value(type)}, {"model", vm::Value("Model")}, {"opaque", vm::Value(vm::Object{{"n", vm::Value(42)}})}});
}
void exercise() {
    auto model = home::HomeRecentGenerationsModel::create();
    std::string focused, opened; std::vector<std::string> edited, reportedKeys; int browsed = 0, selectionEvents = 0;
    model->setHandlers({.focus = [&](std::string key) { focused = std::move(key); },
        .open = [&](std::string token, vm::Value) { opened = std::move(token); },
        .edit = [&](std::vector<std::string> tokens) { edited = std::move(tokens); }, .browse = [&] { ++browsed; },
        .selection = [&](bool, std::vector<std::string> keys) { ++selectionEvents; reportedKeys = std::move(keys); }});
    model->sizeChanged(700, 160);
    check(model->columns.get() == 2 && model->itemWidth.get() == 350 && model->showEmpty.get(), "initial grid geometry or empty state is wrong");
    good(model->setFiles(vm::Value(vm::Array{}), {}, true));
    check(model->emptyMessage.get() == "Reading Society…", "loading state is missing");
    good(model->setAvailabilityMessage("Storage disconnected"));
    good(model->setFiles(vm::Value(vm::Array{}), {}, false));
    check(model->emptyMessage.get()=="Storage disconnected","the unavailable store looked like an empty successful history");
    good(model->setAvailabilityMessage({}));
    check(model->emptyMessage.get()=="No generated images yet","an available empty store kept the failure message");
    const auto a = file("a", "First", "file:///a.png", "Image");
    const auto b = file("b", "Second", "https://example.invalid/b.png", "Image");
    const auto v = file("v", "Video", "file:///v.mp4", "Video");
    const auto values = vm::Value(vm::Array{a, b, v});
    good(model->setFiles(values, {"preview-a", "preview-b", "preview-v"}, false));
    const auto rows = model->rows.get();
    check(rows.size() == 3 && model->canSelect.get() && !model->showEmpty.get() && rows[1].preview == "preview-b", "result projection is wrong");
    good(model->setFocusedKey("b"));
    check(model->focusedKey.get() == "b" && focused.empty(), "external focus update echoed a user command");
    check(!model->setFocusedKey("missing") && model->focusedKey.get() == "b", "invalid focus changed the snapshot");
    good(model->activate(rows[0].token));
    check(opened == rows[0].token && focused == rows[0].key, "normal activation did not open and focus the result");
    good(model->toggleMode());
    check(model->selectionMode.get(), "selection mode was not entered");
    good(model->activate(rows[0].token)); good(model->activate(rows[1].token)); good(model->activate(rows[2].token));
    check(model->selectedCount.get() == 2 && model->canEdit.get(), "selection included video or lost an image");
    check(reportedKeys == std::vector<std::string>({"file:///a.png", "https://example.invalid/b.png"}),
        "selection did not expose the original image identities");
    check(!model->setFiles(vm::Value(vm::Array{a, vm::Value(7)}), {"a", "invalid"}, false) &&
        model->selectedCount.get() == 2 && model->rows.get().size() == 3, "invalid snapshot changed valid state");
    model->editSelection();
    check(edited == std::vector<std::string>({rows[0].token, rows[1].token}), "edit did not target selected originals");
    good(model->setFiles(vm::Value(vm::Array{v, b, a}), {"preview-v", "preview-b", "preview-a"}, false));
    const auto reordered = model->rows.get();
    check(reordered[1].token == rows[1].token && reordered[2].token == rows[0].token && model->selectedCount.get() == 2,
        "result reorder lost identity or selection");
    good(model->setFiles(vm::Value(vm::Array{v, b}), {"preview-v", "preview-b"}, false));
    check(model->selectedCount.get() == 1 && !model->activate(rows[0].token), "removal left stale selection or activation");
    const auto eventsBeforeRestore = selectionEvents;
    good(model->restoreSelection(true, {"https://example.invalid/b.png", "https://example.invalid/b.png", "file:///v.mp4", "missing"}));
    check(model->selectedCount.get() == 1 && selectionEvents == eventsBeforeRestore,
        "external selection restoration echoed a command or accepted a non-image");
    bool wrongThreadRejected = false;
    std::thread outsider([&] { wrongThreadRejected = !model->activate(reordered[1].token); }); outsider.join();
    check(wrongThreadRejected && model->selectedCount.get() == 1, "wrong-thread input changed selection");
    model->sizeChanged(400, 160); check(model->columns.get() == 1 && model->itemWidth.get() == 400, "narrow grid did not use one column");
    good(model->toggleMode()); check(!model->selectionMode.get() && model->selectedCount.get() == 0, "cancel did not clear selection");
    model->browse(); check(browsed == 1, "View all did not navigate");
    check(values == vm::Value(vm::Array{a, b, v}), "review mutated source records");
    model->close(); model->browse(); check(browsed == 1, "closed view retained commands");
}
void reorderDuringFocus() {
    auto model = home::HomeRecentGenerationsModel::create();
    const auto a = file("a", "First", "file:///a.png", "Image");
    const auto b = file("b", "Second", "file:///b.png", "Image");
    good(model->setFiles(vm::Value(vm::Array{a, b}), {"a", "b"}, false));
    const auto token = model->tokenForKey("b");
    vm::Value opened; bool changed = false;
    model->setHandlers({.focus = [&](std::string) {
        if (!changed) { changed = true; good(model->setFiles(vm::Value(vm::Array{b, a}), {"b", "a"}, false)); }
    }, .open = [&](std::string, vm::Value value) { opened = std::move(value); }});
    good(model->activate(token));
    check(opened == b, "focus callback reorder opened a different result");
    model->setHandlers({.focus = [&](std::string) { good(model->setFiles(vm::Value(vm::Array{a}), {"a"}, false)); },
        .open = [&](std::string, vm::Value value) { opened = std::move(value); }});
    opened = {};
    check(!model->activate(token) && opened.kind() == ii::core::ValueKind::null, "removed result remained activatable after focus callback");
    good(model->setFiles(vm::Value(vm::Array{a, b}), {"a", "b"}, false));
    const auto replacement = model->tokenForKey("b");
    model->setHandlers({.focus = [&](std::string) { model->close(); }, .open = [&](std::string, vm::Value value) { opened = std::move(value); }});
    check(!model->activate(replacement) && opened.kind() == ii::core::ValueKind::null, "closed view still opened a result");
}
}
int main() { try { exercise(); reorderDuringFocus(); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; } }
