#include "App/Views/Result/NativeReviewGallery.h"
#include "App/Views/Result/NativeReviewContext.h"
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
    gallery->setEditHandler([&](auto values) { edited = std::move(values); },
                            [](const auto &values) { return values.size()==1; });
    require(!gallery->canEdit.get(),"A provider's single-image constraint did not disable multi-edit");
    auto menu=gallery->contextMenu();
    require(menu->open(session->keyAt(0)) && !menu->context.get().canEdit,
            "The context menu ignored the provider's edit constraint");
    session->clearSelection();session->selectAt(1);
    require(gallery->canEdit.get() && menu->open(session->keyAt(1)) && menu->context.get().canEdit,
            "The provider's valid single target was not editable");
    session->selectAt(0,false,true);
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
    auto navigation = std::make_shared<dreamscapes::review::CollectionNavigation>();
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
void contextActions() {
    using dreamscapes::review::ReviewContextAction;
    auto session=std::make_shared<Session>(); auto menu=dreamscapes::review::ReviewContextModel::create(session);
    const std::vector<Value> entries{entry(0),entry(1),entry(2)};
    session->replaceEntries(entries);
    std::vector<Value> edited,compared,saved,photos;
    menu->setEditHandler([&](auto values){edited=std::move(values);});
    menu->setCompareHandler([&](auto values){compared=std::move(values);});
    menu->setSaveFileHandler([&](auto values){saved=std::move(values);});
    menu->setSavePhotosHandler([&](auto values){photos=std::move(values);});
    session->selectAt(0); session->selectAt(1,false,true);
    require(menu->open(session->keyAt(0)),"The selected result context did not open");
    auto snapshot=menu->context.get();
    require(snapshot.targets==std::vector<Value>({entries[0],entries[1]}) && snapshot.current==entries[0] &&
            snapshot.canCompare && snapshot.canEdit && snapshot.canSaveFile && snapshot.canSavePhotos,
            "Context menu did not target the selected original results");
    session->clearSelection();
    require(menu->invoke(ReviewContextAction::compare) && compared==snapshot.targets,
            "Changing selection replaced the open menu's targets");
    require(menu->invoke(ReviewContextAction::edit) && edited==snapshot.targets,
            "Context edit did not use its original targets");
    require(menu->invoke(ReviewContextAction::saveFile) && saved==std::vector<Value>{entries[0]} &&
            menu->invoke(ReviewContextAction::savePhotos) && photos==saved,
            "Saving from the context menu did not use only the clicked result");
    session->selectAt(0); session->selectAt(1,false,true);
    require(menu->open(session->keyAt(2)),"The unselected result context did not open");
    require(menu->context.get().targets==std::vector<Value>{entries[2]} && !menu->context.get().canCompare,
            "An unselected result inherited an unrelated multi-selection");
    require(menu->invoke(ReviewContextAction::shortlist) && session->reviewState(session->keyAt(2))==State::Shortlisted &&
            session->reviewState(session->keyAt(0))==State::Unreviewed,
            "Context review changed results outside its snapshot");
    require(menu->invoke(ReviewContextAction::reset) && session->reviewState(session->keyAt(2))==State::Unreviewed,
            "Context reset did not restore the result to unreviewed");
    const auto video=Value(Object{{"id",Value("video")},{"mediaType",Value("Video")},{"mediaSource",Value("file:///clip.mp4")}});
    session->replaceEntries({video});
    require(menu->open(session->keyAt(0)),"The video context did not open");
    require(!menu->context.get().canEdit && !menu->context.get().canCompare &&
            menu->context.get().canSaveFile && !menu->context.get().canSavePhotos,
            "Video context exposed image-only actions");
    require(!menu->invoke(ReviewContextAction::edit),"Video context invoked image editing");
    menu->close();

    auto isolated=std::make_shared<Session>(); isolated->replaceEntries({entry(4)});
    auto retired=dreamscapes::review::ReviewContextModel::create(isolated);
    auto closeOnContext=retired->context.observe([&](const auto &value,std::uint64_t) {
        if (!value.targets.empty()) retired->close();
    });
    require(!retired->open(isolated->keyAt(0)) && !retired->active() && retired->context.get().targets.empty(),
            "Reentrant menu retirement revived a closed context");
}
} // namespace
int main() {
    try {
        projectionAndActions();
        previewAndLifetime();
        contextActions();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Native review gallery projection preserves result data and action targets\n";
}
