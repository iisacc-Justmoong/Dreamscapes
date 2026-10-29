#include "App/Views/Result/NativeReviewGalleryView.h"
#include "iiGUI/WidgetState.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void good(ii::core::Result<void> value) {
    if (!value) {
        if (value.error().cause)
            std::rethrow_exception(value.error().cause);
        throw std::runtime_error(value.error().message);
    }
}
template <class T> T take(ii::core::Result<T> value) {
    if (!value) {
        if (value.error().cause)
            std::rethrow_exception(value.error().cause);
        throw std::runtime_error(value.error().message);
    }
    return std::move(*value);
}
vm::Value entry(int index) {
    const auto id = std::to_string(index);
    return vm::Value(vm::Object{{"id", vm::Value(id)},
                                {"documentSource", vm::Value("file:///review/" + id + ".iisc")},
                                {"mediaType", vm::Value("Image")}});
}
void exercise() {
    auto state = std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {960, 600}));
    std::shared_ptr<review::ReviewGalleryModel> gallery;
    std::vector<vm::Value> entries;
    std::vector<vm::Value> edited, compared, detailed;
    for (int index = 0; index < 1000; ++index)
        entries.push_back(entry(index));
    good(root.set_content([&](ii::gui::WidgetBuilder &builder) {
        return review::buildReviewGallery(builder, {}, [&](const auto &model) {
            gallery = model;
            model->setEditHandler([&](auto values) { edited = std::move(values); });
            model->setCompareHandler([&](auto values) { compared = std::move(values); });
            model->setDetailHandler([&](auto values) { detailed = std::move(values); });
            model->session()->replaceEntries(entries);
        });
    }));
    auto list = take(root.content_item("results"));
    auto row = [&](std::size_t index) {
        return take(list.keyed_content_item(gallery->items.get().at(index)->token, "result"));
    };
    check(take(state->snapshots()).size() < 200, "The gallery instantiated all offscreen widgets");
    auto click = [&](std::size_t index, bool shift = false, bool meta = false) {
        const auto bounds = take(row(index).snapshot()).bounds;
        good(state->input({.kind = ii::gui::WidgetInputKind::pointer_down,
                           .x = bounds.x + 2,
                           .y = bounds.y + 2,
                           .shift = shift,
                           .meta = meta,
                           .buttons = 1}));
        good(state->input({.kind = ii::gui::WidgetInputKind::pointer_up,
                           .x = bounds.x + 2,
                           .y = bounds.y + 2,
                           .shift = shift,
                           .meta = meta,
                           .buttons = 1}));
    };
    click(1);
    check(gallery->session()->focusedKey() == gallery->items.get()[1]->key && gallery->session()->selectedCount() == 0,
          "A plain result click did not exclusively focus");
    click(3, true);
    check(gallery->session()->selectedCount() == 3, "Shift-click did not select the expected result range");
    good(take(root.content_item("edit")).activate());
    check(edited == std::vector<vm::Value>{entries[1], entries[2], entries[3]},
          "The native edit button did not receive the selected original results");
    good(list.focus());
    auto key = [&](std::string text) {
        good(state->input({.kind = ii::gui::WidgetInputKind::key_down,
                           .key = ii::gui::WidgetKey::character,
                           .text = std::move(text)}));
    };
    key("f");
    check(gallery->session()->shortlistedCount() == 3, "The shortlist keyboard shortcut did not review the selection");
    key("x");
    check(gallery->session()->rejectedCount() == 3, "The reject keyboard shortcut did not review the selection");
    key("c");
    check(compared == edited, "The compare keyboard shortcut lost its original targets");
    good(state->input({.kind = ii::gui::WidgetInputKind::key_down,
                       .key = ii::gui::WidgetKey::character,
                       .text = "f",
                       .text_input_active = true}));
    check(gallery->session()->rejectedCount() == 3, "A text-editing keystroke changed the review state");
    const auto focusBeforeDetail = gallery->session()->focusedEntry();
    const auto selectionBeforeDetail = gallery->session()->selectedCount();
    good(state->input({.kind = ii::gui::WidgetInputKind::key_down, .key = ii::gui::WidgetKey::space}));
    check(detailed == std::vector<vm::Value>{focusBeforeDetail} &&
              gallery->session()->selectedCount() == selectionBeforeDetail,
          "Space did not open the focused result without changing selection");
    good(take(root.content_item("shortlist")).activate());
    check(gallery->session()->shortlistedCount() == 3, "The native shortlist button lost its selected targets");
    auto scroll = take(root.content_item("results/viewport"));
    good(scroll.set_scroll_offset(800));
    const auto offset = take(scroll.snapshot()).scroll_offset;
    check(offset > 0, "The virtual result list did not scroll");
    const auto focused = gallery->session()->focusedKey();
    entries.push_back(entry(1000));
    gallery->session()->replaceEntries(entries);
    check(gallery->session()->selectedCount() == 3 && gallery->session()->focusedKey() == focused &&
              take(scroll.snapshot()).scroll_offset == offset,
          "An arriving result changed the active selection, focus or scroll position");
    good(take(root.content_item("shortlisted")).activate());
    check(gallery->items.get().size() == 3 && gallery->session()->selectedCount() == 3,
          "The native filter lost the shortlist or selection");
    state->close();
}
} // namespace
int main() {
    try {
        exercise();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Native gallery widgets retain selection and virtualize rows without a window\n";
}
