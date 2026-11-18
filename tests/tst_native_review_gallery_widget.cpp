#include "App/Views/Result/NativeReviewGalleryView.h"
#include <LVUI>
#include "iiGUI/WidgetState.h"
#include <algorithm>
#include <iostream>
#include <set>
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
                                {"thumbnailSource", vm::Value("file:///review/" + id + ".png")},
                                {"mediaType", vm::Value("Image")}});
}
class PreviewResolver final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    mutable std::set<std::string> requested;
    mutable ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> ready;
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view address, std::uint32_t,
                                                       std::uint32_t) const override {
        requested.emplace(address);
        return ready.get();
    }
};
void exercise() {
    auto state = std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {960, 600}));
    std::shared_ptr<review::ReviewGalleryModel> gallery;
    auto resolver = std::make_shared<PreviewResolver>();
    std::vector<vm::Value> entries;
    std::vector<vm::Value> edited, compared, detailed;
    for (int index = 0; index < 1000; ++index)
        entries.push_back(entry(index));
    good(root.set_content([&](ii::gui::WidgetBuilder &builder) {
        return review::buildReviewGallery(builder, {}, [&](const auto &model) {
            gallery = model;
            model->setImageResolver(resolver);
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
    auto imageCount = [&] {
        const auto frame = take(state->drawing_snapshot({960, 600}));
        auto count = frame.images.size();
        for (const auto &layer : frame.overlays)
            count += layer.images.size();
        for (const auto &scene : frame.scenes)
            for (const auto &layer : scene.layers)
                count += layer.images.size();
        return count;
    };
    const auto firstItem = gallery->items.get().front();
    check(!firstItem->token.empty() && firstItem->thumbnailAddress.get() == "file:///review/0.png",
          "The first review row lost its preview identity or address");
    check(take(state->snapshots()).size() < 200, "The gallery instantiated all offscreen widgets");
    if (!resolver->requested.contains("file:///review/0.png") || resolver->requested.size() >= 100)
        throw std::runtime_error("The gallery did not request only visible result previews: " +
                                 std::to_string(resolver->requested.size()) + " sources");
    check(imageCount() == 0, "An unavailable preview appeared ready");
    auto pixels = take(ii::gui::rgba_image(1, 1, {255, 0, 0, 255}));
    (void)resolver->ready.set(pixels);
    check(imageCount() > 0, "A completed preview did not update its native image");
    gallery->setImageResolver({});
    check(imageCount() == 0, "Removing the preview resolver kept stale pixels");
    const auto first = gallery->items.get().front();
    check(gallery->setThumbnail(first->key, first->source, pixels), "The supplied thumbnail was rejected");
    check(imageCount() > 0, "A supplied thumbnail was not displayed");
    gallery->setImageResolver(resolver);
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
    check(detailed.empty(), "A single result click opened the detail view");
    const auto bounds = take(row(1).snapshot()).bounds;
    for (std::uint32_t count=1;count<=2;++count) {
        good(state->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=bounds.x+2,.y=bounds.y+2,
                           .timestamp=double(count),.buttons=1,.native_click_count=count}));
        good(state->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=bounds.x+2,.y=bounds.y+2,
                           .timestamp=double(count)+0.1,.buttons=1,.native_click_count=count}));
    }
    check(detailed==std::vector<vm::Value>{entries[1]} && gallery->session()->selectedCount()==0,
          "Double-click did not open the focused result without changing selection");
    click(3, true);
    check(gallery->session()->selectedCount() == 3, "Shift-click did not select the expected result range");
    const auto contextBounds=take(row(3).snapshot()).bounds;
    good(state->input({.kind=ii::gui::WidgetInputKind::context_request,.x=contextBounds.x+2,.y=contextBounds.y+2}));
    const auto selectedToken=gallery->items.get()[3]->token;
    const auto menu=take(list.keyed_content_item(selectedToken,"reviewContextMenu"));
    check(take(menu.snapshot()).opened,"A result context request did not open its menu");
    good(take(list.keyed_content_item(selectedToken,"reviewContextCompare")).activate());
    check(compared==std::vector<vm::Value>{entries[1],entries[2],entries[3]},
          "The context menu did not compare the selected original results");
    const auto unselectedBounds=take(row(0).snapshot()).bounds;
    const auto unselectedToken=gallery->items.get()[0]->token;
    const auto openUnselected=[&] {
        good(state->input({.kind=ii::gui::WidgetInputKind::context_request,.x=unselectedBounds.x+2,.y=unselectedBounds.y+2}));
        check(take(take(list.keyed_content_item(unselectedToken,"reviewContextMenu")).snapshot()).opened,
              "The unselected result context did not open");
    };
    openUnselected();
    const auto label=[](std::size_t count){return "Candidates: "+std::to_string(count);};
    gallery->contextMenu()->setStrings({label,label,label,label,label,"Save file","Save photo"});
    const auto nodes=take(state->snapshots());
    check(std::ranges::any_of(nodes,[](const auto &node){return node.text=="Candidates: 1";}),
          "Context labels did not update after the host supplied translations");
    good(take(list.keyed_content_item(unselectedToken,"reviewContextShortlist")).activate());
    check(gallery->session()->shortlistedCount()==1 && gallery->session()->selectedCount()==3,
          "The context menu applied review to unrelated selected results");
    openUnselected();
    good(take(list.keyed_content_item(unselectedToken,"reviewContextReset")).activate());
    check(gallery->session()->shortlistedCount()==0,"The context menu could not reset a single result");
    good(take(root.content_item("edit")).activate());
    check(edited == std::vector<vm::Value>{entries[1], entries[2], entries[3]},
          "The native edit button did not receive the selected original results");
    good(list.focus());
    edited.clear();
    good(state->input({.kind=ii::gui::WidgetInputKind::key_down,.key=ii::gui::WidgetKey::enter}));
    check(edited==std::vector<vm::Value>{entries[1],entries[2],entries[3]},
          "Enter did not edit the selected results");
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

void compareWorkspace() {
    auto state=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) {task();});
    auto root=take(state->content_host({},"compareRoot",{960,600}));
    std::shared_ptr<review::ReviewGalleryModel> gallery;
    auto resolver=std::make_shared<PreviewResolver>();std::vector<vm::Value> entries;
    for (int i=0;i<80;++i) entries.push_back(entry(i));
    good(root.set_content([&](ii::gui::WidgetBuilder &builder) {
        return review::buildReviewGallery(builder,{},[&](const auto &model) {
            gallery=model;model->setImageResolver(resolver);model->session()->replaceEntries(entries);
        });
    }));
    (void)take(state->drawing_snapshot({960,600}));
    gallery->session()->selectAt(0);gallery->session()->selectAt(1,false,true);
    auto scroll=take(root.content_item("results/viewport"));good(scroll.set_scroll_offset(400));
    const auto offset=take(scroll.snapshot()).scroll_offset;
    check(offset>0,"Comparison test needs a scrolled gallery");
    const auto focused=gallery->session()->focusedKey();
    good(take(root.content_item("compare")).activate());
    (void)take(state->drawing_snapshot({960,600}));
    check(resolver->requested.contains("file:///review/0.iisc") && resolver->requested.contains("file:///review/1.iisc"),
          "Comparison did not resolve both selected original sources");
    auto pixels=take(ii::gui::rgba_image(4,4,std::vector<std::uint8_t>(4*4*4,255)));
    (void)resolver->ready.set(pixels);
    const auto frame=take(state->drawing_snapshot({960,600}));
    std::size_t images=frame.images.size();
    for (const auto &layer:frame.overlays) images+=layer.images.size();
    for (const auto &scene:frame.scenes) for (const auto &layer:scene.layers) images+=layer.images.size();
    check(images>=2,"Comparison did not render the loaded previews");
    const auto beforeFocus=gallery->session()->focusedKey();
    good(state->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=500,.y=300,.buttons=1}));
    good(state->input({.kind=ii::gui::WidgetInputKind::pointer_move,.x=510,.y=310,.buttons=1}));
    good(state->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=510,.y=310,.buttons=1}));
    check(gallery->session()->focusedKey()==beforeFocus && gallery->session()->selectedCount()==2,
          "Pointer interaction in comparison reached the obscured gallery");
    entries.push_back(entry(80));gallery->session()->replaceEntries(entries);
    check(gallery->session()->selectedCount()==2 && gallery->session()->focusedKey()==focused,
          "An arriving result changed the active comparison selection");
    good(take(root.content_item("compareActual")).activate());
    good(take(root.content_item("compareSync")).activate());
    check(take(take(root.content_item("compareSync")).snapshot()).text=="Sync off",
          "Comparison synchronization control did not update");
    good(take(root.content_item("compareActual")).focus());
    good(state->input({.kind=ii::gui::WidgetInputKind::key_down,.key=ii::gui::WidgetKey::escape}));
    check(!take(take(root.content_item("compareSurface")).snapshot()).visible,
          "Escape did not close comparison from a focused control");
    good(take(root.content_item("compare")).activate());
    (void)take(state->drawing_snapshot({960,600}));
    good(take(root.content_item("compareBack")).activate());
    const auto finalOffset=take(scroll.snapshot()).scroll_offset;
    if (gallery->session()->selectedCount()!=2 || gallery->session()->focusedKey()!=focused || finalOffset!=offset)
        throw std::runtime_error("Closing comparison changed selection, focus or gallery scroll: selected="+
            std::to_string(gallery->session()->selectedCount())+" focus="+std::to_string(gallery->session()->focusedKey()==focused)+
            " offset="+std::to_string(offset)+" -> "+std::to_string(finalOffset));
    state->close();
}
} // namespace
int main() {
    try {
        exercise();
        compareWorkspace();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Native gallery widgets retain selection and virtualize rows without a window\n";
}
