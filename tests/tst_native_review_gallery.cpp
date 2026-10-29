#include "App/Views/Result/NativeReviewGallery.h"
#include <iostream>
#include <stdexcept>

using dreamscapes::review::ReviewGalleryModel;
using dreamscapes::review::Session;
using dreamscapes::review::State;
using dreamscapes::vm::Object;
using dreamscapes::vm::Value;

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
Value entry(int id) {
    return Value(Object{{"id", Value(std::to_string(id))},
                        {"documentSource", Value("file:///gallery/" + std::to_string(id) + ".iisc")},
                        {"thumbnailSource", Value("file:///gallery/" + std::to_string(id) + ".png")},
                        {"mediaType", Value("Image")}});
}
void projectionAndActions() {
    auto session = std::make_shared<Session>();
    auto gallery = ReviewGalleryModel::create(session);
    const std::vector<Value> entries{entry(0), entry(1), entry(2)};
    require(!gallery->canReview.get() && !gallery->hasSelection.get(), "An empty gallery exposed result actions");
    session->replaceEntries(entries);
    require(gallery->items.get().size() == 3 && gallery->resultSummary.get() == "3 results",
            "Actual results did not enter the native gallery projection");
    auto first = gallery->items.get().front();
    const auto revision = gallery->items.revision();
    session->focusAt(1);
    require(gallery->items.revision() == revision && gallery->items.get().front() == first,
            "Focus movement rebuilt the gallery items");
    session->selectAt(0);
    require(first->selected.get() && gallery->selectionSummary.get() == "1 selected" && !gallery->canEdit.get(),
            "Selection changed the wrong row or enabled an unavailable action");
    require(gallery->canReview.get() && gallery->hasSelection.get(),
            "Available review and clear actions did not follow selection");
    std::vector<Value> edited, compared, exported;
    gallery->setEditHandler([&](auto values) { edited = std::move(values); });
    gallery->setCompareHandler([&](auto values) { compared = std::move(values); });
    gallery->setExportHandler([&](auto values) { exported = std::move(values); });
    require(gallery->canEdit.get() && gallery->canExport.get() && !gallery->canCompare.get(),
            "Native action availability did not reflect its actual targets");
    gallery->edit();
    require(edited == std::vector<Value>{entries[0]}, "Edit did not receive the original result metadata");
    session->selectAt(1, false, true);
    require(gallery->canCompare.get(), "Two selected images could not be compared");
    gallery->compare();
    gallery->exportResults();
    require(compared == std::vector<Value>({entries[0], entries[1]}) && compared == exported,
            "Compare or export changed the selected original results");
    session->setReview(State::Shortlisted);
    session->setFilter("shortlisted");
    require(gallery->items.get().size() == 2 && gallery->items.get().front() == first && session->selectedCount() == 2,
            "Filtering discarded result identity or selection");
    const auto hiddenKey = Session::keyForEntry(entries[2]);
    const auto oldSource = Value("file:///gallery/2.png");
    auto changed = Value(Object{{"id", Value("2")},
                                {"documentSource", Value("file:///gallery/2.iisc")},
                                {"thumbnailSource", Value("file:///gallery/new.png")},
                                {"mediaType", Value("Image")}});
    session->replaceEntries({entries[0], entries[1], changed});
    require(!gallery->setThumbnail(hiddenKey, oldSource, {}),
            "A hidden result accepted a preview for a replaced source");
}
void previewAndLifetime() {
    auto session = std::make_shared<Session>();
    auto gallery = ReviewGalleryModel::create(session);
    session->replaceEntries({entry(0)});
    const auto key = session->keyAt(0);
    auto item = gallery->items.get().front();
    require(!gallery->setThumbnail(key, Value("old source"), {}), "A stale preview replaced a current source");
    require(gallery->setThumbnail(key, item->source, {}), "A current preview could not be cleared");
    auto navigation = std::make_shared<lvui::native::CollectionNavigation>();
    gallery->bindNavigation(navigation);
    require(gallery->navigation()->navigation().state.get().current_key == gallery->items.get().front()->token,
            "Gallery binding did not adopt the shared LVUI navigation");
    session->replaceEntries({entry(0), entry(1)});
    require(gallery->items.get().size() == 2 && !gallery->items.get().back()->token.empty() &&
                gallery->navigation()->keyForToken(gallery->items.get().back()->token) == session->keyAt(1),
            "A result arriving after shared navigation binding has no valid identity");
    const auto tokens = gallery->navigation()->visibleTokens();
    auto source = lvui::native::virtual_collection_source(tokens, [](const auto &value, std::size_t) { return value; });
    require(static_cast<bool>(source), "Gallery navigation source could not be created");
    navigation->focus_at(*source, 0);
    navigation->focus_at(*source, 1, {}, false);
    require(session->navigationState().anchorKey == session->keyAt(1) && session->selectedCount() == 1,
            "A focus-only click did not establish the product range anchor");
    navigation->focus_at(*source, 0, {.range = true});
    require(session->selectedCount() == 2, "Range selection did not use the focus-only anchor");
    session->replaceEntries({entry(1)});
    session->replaceEntries({entry(1), entry(0)});
    const auto beforeRebind = navigation->state.get();
    auto retainedItem = gallery->items.get().front();
    gallery->bindNavigation(navigation);
    require(navigation->state.get() == beforeRebind && gallery->items.get().front() == retainedItem &&
                gallery->navigation()->keyForToken(navigation->state.get().current_key) == session->focusedKey(),
            "Rebinding the same navigation replaced live identities or state");
    gallery->close();
    session->selectAt(0);
    require(session->selectedCount() == 1, "Closing the gallery damaged its product session");
    navigation->set_current({});
}
} // namespace
int main() {
    try {
        projectionAndActions();
        previewAndLifetime();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Native review gallery projection preserves result data and action targets\n";
}
