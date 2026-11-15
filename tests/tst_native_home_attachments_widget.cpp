#include "App/Views/Home/NativeHomeAttachmentsView.h"
#include "iiGUI/WidgetState.h"
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
    throw std::runtime_error("Missing attachment widget " + std::string(id));
}
class Images final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    mutable std::vector<std::string> requests;
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view source, std::uint32_t, std::uint32_t) const override {
        requests.emplace_back(source); return {};
    }
};
void exercise() {
    using namespace ii::gui;
    auto composer = HomeComposerModel::create(); HomeComposerSnapshot snapshot;
    snapshot.attachments = {{"a", "one.png", "file:///one.png", "1 MB"}, {"b", "two.png", "file:///two.png", "2 MB"}};
    good(composer->setSnapshot(snapshot)); int calls = 0; std::string removed;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        ++calls; removed = command.value; return {};
    }));
    auto model = HomeAttachmentsModel::create(composer, [](const auto &name) { return "Remove " + name; });
    auto images = std::make_shared<Images>();
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {240,44}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeAttachments(builder, {}, model, images); }));
    (void)take(state->drawing_snapshot({240,44}));
    auto list = find(state,"homeAttachments"); auto item = take(list.keyed_content_item("a", "homeAttachmentRemove"));
    auto scroll = find(state,"homeAttachmentsScroll");
    check(take(scroll.snapshot()).content_width > 240, "attachment cards do not create horizontal scrollable content");
    good(scroll.set_horizontal_scroll_offset(150));
    check(take(scroll.snapshot()).horizontal_scroll_offset > 0, "attachment list did not scroll horizontally");
    check(take(item.snapshot()).accessibility_name == "Remove one.png" && !images->requests.empty(),
          "attachment card lost its accessible action or preview");
    std::swap(snapshot.attachments[0], snapshot.attachments[1]); good(composer->setSnapshot(snapshot));
    check(take(item.snapshot()).accessibility_name == "Remove one.png", "reordering replaced the stable attachment action");
    check(take(scroll.snapshot()).horizontal_scroll_offset > 0, "attachment reorder reset the user's scroll position");
    good(item.activate()); check(calls == 1 && removed == "a", "reordered card removed the wrong attachment");
    snapshot.attachments.pop_back(); good(composer->setSnapshot(snapshot));
    check(!item.activate() && calls == 1, "retired card removed a stale attachment");
    good(composer->setHandler({}));
    auto remaining = take(list.keyed_content_item("b", "homeAttachmentRemove"));
    check(!take(remaining.snapshot()).enabled, "retired canvas left attachment controls enabled");
    state->close(); model->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
