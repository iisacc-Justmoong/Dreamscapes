#include "App/Views/Home/NativeHomeAttachments.h"
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
    auto composer = HomeComposerModel::create(); int calls = 0; std::string removed;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        check(command.kind == HomeComposerCommandKind::removeAttachment, "unexpected attachment command");
        ++calls; removed = command.value; return {};
    }));
    auto model = HomeAttachmentsModel::create(composer, [](const auto &name) { return "Remove " + name; });
    check(!model->visible.get() && model->enabled.get(), "empty attachment list state is incorrect");
    auto next = composer->snapshot.get();
    next.attachments = {{"a", "one.png", "file:///one.png", "1 MB"}, {"b", "two.png", "file:///two.png", "2 MB"}};
    good(composer->setSnapshot(next));
    check(model->visible.get() && model->rows.get().size() == 2 && model->rows.get()[0].removeAccessible == "Remove one.png" && calls == 0,
          "attachment projection lost data or made a command");
    std::swap(next.attachments[0], next.attachments[1]); good(composer->setSnapshot(next));
    good(model->remove("a")); check(calls == 1 && removed == "a", "attachment removal used a stale position");
    next.attachments.pop_back(); good(composer->setSnapshot(next));
    check(!model->remove("a") && calls == 1, "removed attachment was acted on again");
    good(model->setViewport(240, 44)); check(model->viewportWidth.get() == 240, "attachment viewport was lost");
    check(!model->setViewport(std::numeric_limits<double>::infinity(), 44), "invalid attachment viewport accepted");
    next.attachments.clear(); good(composer->setSnapshot(next)); check(!model->visible.get(), "empty attachments remained visible");
    auto detach = model->rows.observe([&](const auto &, std::uint64_t) { good(composer->setHandler({})); });
    next.attachments = {{"c", "three.png", "", ""}}; good(composer->setSnapshot(next));
    check(!model->enabled.get() && !model->remove("c"), "retired canvas remained active during publication");
    model->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
