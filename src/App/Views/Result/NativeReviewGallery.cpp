#include "NativeReviewGallery.h"
#include "NativeReviewCompareView.h"
#include <stdexcept>
#include <utility>

namespace dreamscapes::review {
namespace {
std::string menuLabel(const std::weak_ptr<ReviewContextModel> &weak,ReviewContextAction action) {
    auto menu=weak.lock(); return menu && menu->active()?menu->label(action):std::string{};
}
bool menuEnabled(const std::weak_ptr<ReviewContextModel> &weak,ReviewContextAction action) {
    auto menu=weak.lock(); return menu && menu->active() && menu->enabled(action);
}
}
void ReviewGalleryItem::openMenu() {if (auto menu=contextOwner.lock(); menu && menu->active()) (void)menu->open(key);}
void ReviewGalleryItem::menuAction(int value) {
    if (value<0 || value>static_cast<int>(ReviewContextAction::savePhotos)) return;
    if (auto menu=contextOwner.lock(); menu && menu->active()) (void)menu->invoke(static_cast<ReviewContextAction>(value));
}
std::string ReviewGalleryItem::shortlistLabel() const {return menuLabel(contextOwner,ReviewContextAction::shortlist);}
std::string ReviewGalleryItem::rejectLabel() const {return menuLabel(contextOwner,ReviewContextAction::reject);}
std::string ReviewGalleryItem::resetLabel() const {return menuLabel(contextOwner,ReviewContextAction::reset);}
std::string ReviewGalleryItem::compareLabel() const {return menuLabel(contextOwner,ReviewContextAction::compare);}
std::string ReviewGalleryItem::editLabel() const {return menuLabel(contextOwner,ReviewContextAction::edit);}
std::string ReviewGalleryItem::saveFileLabel() const {return menuLabel(contextOwner,ReviewContextAction::saveFile);}
std::string ReviewGalleryItem::savePhotosLabel() const {return menuLabel(contextOwner,ReviewContextAction::savePhotos);}
bool ReviewGalleryItem::canContextCompare() const {return menuEnabled(contextOwner,ReviewContextAction::compare);}
bool ReviewGalleryItem::canContextEdit() const {return menuEnabled(contextOwner,ReviewContextAction::edit);}
bool ReviewGalleryItem::canContextSaveFile() const {return menuEnabled(contextOwner,ReviewContextAction::saveFile);}
bool ReviewGalleryItem::canContextSavePhotos() const {return menuEnabled(contextOwner,ReviewContextAction::savePhotos);}

std::shared_ptr<ReviewGalleryModel> ReviewGalleryModel::create(std::shared_ptr<Session> session) {
    auto result = std::shared_ptr<ReviewGalleryModel>(new ReviewGalleryModel(std::move(session)));
    result->start();
    return result;
}

ReviewGalleryModel::ReviewGalleryModel(std::shared_ptr<Session> session) : session_(std::move(session)) {
    if (!session_)
        throw std::invalid_argument("A review gallery requires a session");
}

ReviewGalleryModel::~ReviewGalleryModel() {
    if (active_ && owner_ == std::this_thread::get_id())
        close();
}

void ReviewGalleryModel::requireOwner() const {
    if (owner_ != std::this_thread::get_id())
        throw std::logic_error("Review gallery requires its owner thread");
    if (!active_)
        throw std::logic_error("Review gallery is closed");
}

void ReviewGalleryModel::start() {
    requireOwner();
    context_ = ReviewContextModel::create(session_);
    navigation_ = ReviewNavigation::create(session_);
    const std::weak_ptr<ReviewGalleryModel> weak = shared_from_this();
    observer_ = std::make_shared<ChangePort>([weak](const Change &change) {
        if (auto owner = weak.lock(); owner && owner->active_)
            owner->update(change);
    });
    if (const auto result = session_->observe(observer_); !result)
        throw std::runtime_error(result.error().message);
    rebuildItems();
    updateActions();
}

std::shared_ptr<Session> ReviewGalleryModel::session() const {
    requireOwner();
    return session_;
}

std::shared_ptr<ReviewNavigation> ReviewGalleryModel::navigation() const {
    requireOwner();
    return navigation_;
}
std::shared_ptr<ReviewContextModel> ReviewGalleryModel::contextMenu() const {requireOwner(); return context_;}

void ReviewGalleryModel::bindNavigation(std::shared_ptr<dreamscapes::review::CollectionNavigation> navigation) {
    requireOwner();
    if (navigation && navigation.get() == &navigation_->navigation())
        return;
    auto replacement = ReviewNavigation::bind(session_, std::move(navigation));
    // Register after the new binding so it has assigned tokens before rows
    // are projected when another result arrives.
    const std::weak_ptr<ReviewGalleryModel> weak = shared_from_this();
    auto replacementObserver = std::make_shared<ChangePort>([weak](const Change &change) {
        if (auto owner = weak.lock(); owner && owner->active_)
            owner->update(change);
    });
    if (const auto result = session_->observe(replacementObserver); !result)
        throw std::runtime_error(result.error().message);
    navigation_->close();
    navigation_ = std::move(replacement);
    observer_ = std::move(replacementObserver);
    auto previous = std::move(itemsByKey_);
    itemsByKey_.clear();
    rebuildItems(&previous);
}

void ReviewGalleryModel::update(const Change &change) {
    requireOwner();
    if (change.entriesChanged) {
        // A hidden row may have received a new thumbnail source. Discard its
        // cached projection so a late decode for the old source cannot land.
        std::erase_if(itemsByKey_, [&](const auto &entry) { return session_->indexForKey(entry.first) < 0; });
    }
    if (change.projection != ProjectionChange::None || change.entriesChanged) {
        rebuildItems();
    } else {
        for (const auto &changed : change.rows) {
            if (const auto row = session_->rowAt(session_->indexForKey(changed.key)); row)
                updateItem(*row);
        }
    }
    updateActions();
}

void ReviewGalleryModel::rebuildItems(const std::unordered_map<std::string, Item> *previous) {
    std::vector<Item> visible;
    visible.reserve(session_->visibleCount());
    for (std::size_t i = 0; i < session_->visibleCount(); ++i) {
        const auto row = session_->rowAt(static_cast<int>(i));
        if (!row)
            throw std::logic_error("Visible review result has no row");
        updateItem(*row);
        const auto &item = itemsByKey_.at(row->key);
        if (previous) {
            const auto found = previous->find(row->key);
            if (found != previous->end() && found->second->source == item->source)
                (void)item->thumbnail.set(found->second->thumbnail.get());
        }
        visible.push_back(item);
    }
    (void)items.set(std::move(visible));
}

void ReviewGalleryModel::updateItem(const Row &row) {
    auto &item = itemsByKey_[row.key];
    if (!item) {
        item = std::make_shared<ReviewGalleryItem>();
        item->key = row.key;
        item->token = navigation_->tokenForKey(row.key);
        item->contextOwner = context_;
    }
    if (item->source != row.thumbnail) {
        item->source = row.thumbnail;
        // Clear the old address before clearing its pixels so an observation
        // cannot start another request for the displaced preview.
        (void)item->thumbnailAddress.set({});
        (void)item->thumbnail.set({});
        (void)item->thumbnailAddress.set(vm::text(row.thumbnail));
    }
    (void)item->title.set(row.title);
    (void)item->mediaType.set(row.mediaType);
    (void)item->jobId.set(vm::text(vm::get(row.entry,"id")));
    (void)item->ordinal.set(row.ordinal);
    (void)item->rejected.set(row.review==State::Rejected);
    (void)item->badgeVisible.set(row.selected || row.review!=State::Unreviewed);
    (void)item->badge.set(std::string(row.selected?"✓ ":"") +
        (row.review==State::Shortlisted?"★":row.review==State::Rejected?"×":""));
    (void)item->selected.set(row.selected);
    (void)item->focused.set(row.focused);
    const char *label = row.review == State::Shortlisted ? "Shortlisted"
                        : row.review == State::Rejected  ? "Rejected"
                                                         : "Unreviewed";
    (void)item->reviewLabel.set(std::string(row.selected ? "Selected · " : row.focused ? "Focused · " : "") + label);
}

bool ReviewGalleryModel::setThumbnail(std::string_view key, const vm::Value &expectedSource,
                                      std::shared_ptr<const ii::gui::RasterImage> image) {
    requireOwner();
    const auto it = itemsByKey_.find(std::string(key));
    if (it == itemsByKey_.end() || it->second->source != expectedSource)
        return false;
    (void)it->second->thumbnail.set(std::move(image));
    return true;
}

void ReviewGalleryModel::setImageResolver(std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> resolver) {
    requireOwner();
    (void)imageResolver_.set(std::move(resolver));
}
bool ReviewGalleryModel::clearImageResolver(const std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> &expected) {
    // The native surface may retire after its gallery session has closed, but
    // its image workers and event loop must still be released in that order.
    if (owner_ != std::this_thread::get_id())
        throw std::logic_error("Review gallery requires its owner thread");
    if (imageResolver_.get()!=expected) return false;
    (void)imageResolver_.set({}); return true;
}

std::shared_ptr<const ii::gui::RasterImage>
ReviewGalleryModel::imageForToken(std::string_view token, std::uint32_t width, std::uint32_t height) const {
    requireOwner();
    const auto key = navigation_->keyForToken(token);
    const auto found = itemsByKey_.find(key);
    if (found == itemsByKey_.end())
        return {};
    const auto &item = found->second;
    if (auto pixels = item->thumbnail.get())
        return pixels;
    const auto address = item->thumbnailAddress.get();
    const auto resolver = imageResolver_.get();
    return address.empty() || !resolver ? nullptr : resolver->image(address, width, height);
}
std::optional<dreamscapes::native::PreviewStatus>
ReviewGalleryModel::previewStatusForToken(std::string_view token,std::uint32_t width,std::uint32_t height) const {
    requireOwner();
    const auto key=navigation_->keyForToken(token); const auto found=itemsByKey_.find(key);
    if (found==itemsByKey_.end()) return {};
    const auto &item=found->second;
    if (item->thumbnail.get()) return dreamscapes::native::PreviewStatus::ready;
    const auto address=item->thumbnailAddress.get();
    if (address.empty()) return dreamscapes::native::PreviewStatus::loading;
    auto status=std::dynamic_pointer_cast<const dreamscapes::native::PreviewStatusSource>(imageResolver_.get());
    return status?std::optional{status->previewStatus(address,width,height)}:std::nullopt;
}

ReviewGalleryModel::ResolvedPreview ReviewGalleryModel::previewForSource(std::string_view source,std::uint32_t width,std::uint32_t height) const {
    requireOwner();const auto resolver=imageResolver_.get();
    if (source.empty() || !resolver) return {{},"Preview unavailable"};
    if (auto image=resolver->image(source,width,height)) return {std::move(image),{}};
    const auto status=std::dynamic_pointer_cast<const dreamscapes::native::PreviewStatusSource>(resolver);
    return {{},status && status->previewStatus(source,width,height)==dreamscapes::native::PreviewStatus::loading?
                "Loading preview…":"Preview unavailable"};
}

void ReviewGalleryModel::updateActions() {
    (void)resultSummary.set(std::to_string(session_->totalCount()) + " results");
    (void)selectionSummary.set(std::to_string(session_->selectedCount()) + " selected");
    const auto action = session_->actionEntries();
    (void)canReview.set(!action.empty());
    (void)hasSelection.set(session_->selectedCount() != 0);
    (void)canEdit.set(canEditEntries(action));
    (void)canCompare.set(static_cast<bool>(compare_) && Session::validImages(session_->selectedEntries(), 2, 4));
    (void)canExport.set(static_cast<bool>(export_) && !action.empty());
}

bool ReviewGalleryModel::canEditEntries(const std::vector<vm::Value> &entries) const {
    if (!edit_ || !Session::validImages(entries)) return false;
    try {return !editEligible_ || editEligible_(entries);} catch (...) {return false;}
}
void ReviewGalleryModel::setEditHandler(Action handler,Eligibility eligible) {
    requireOwner();
    const auto keepAlive=shared_from_this(); auto contextHandler=handler;auto contextEligibility=eligible;
    edit_ = std::move(handler);editEligible_=std::move(eligible);
    context_->setEditHandler(std::move(contextHandler),std::move(contextEligibility));
    if (!active_) return;
    updateActions();
}
void ReviewGalleryModel::setCompareHandler(Action handler) {
    requireOwner();
    const auto keepAlive=shared_from_this(); auto contextHandler=handler;
    compare_ = std::move(handler);
    context_->setCompareHandler(std::move(contextHandler));
    if (!active_) return;
    updateActions();
}
void ReviewGalleryModel::setExportHandler(Action handler) {
    requireOwner();
    export_ = std::move(handler);
    updateActions();
}
void ReviewGalleryModel::setDetailHandler(Action handler) {
    requireOwner();
    detail_ = std::move(handler);
}
void ReviewGalleryModel::setSaveFileHandler(Action handler,Eligibility eligible) {
    requireOwner();context_->setSaveFileHandler(std::move(handler),std::move(eligible));
}
void ReviewGalleryModel::setSavePhotosHandler(Action handler) {requireOwner(); context_->setSavePhotosHandler(std::move(handler));}
void ReviewGalleryModel::edit() {
    requireOwner();
    auto action = edit_;
    auto values = session_->actionEntries();
    if (action && canEditEntries(values))
        action(std::move(values));
}
void ReviewGalleryModel::compare() {
    requireOwner();
    auto action = compare_;
    auto values = session_->selectedEntries();
    if (action && Session::validImages(values, 2, 4))
        action(std::move(values));
}
void ReviewGalleryModel::exportResults() {
    requireOwner();
    auto action = export_;
    auto values = session_->actionEntries();
    if (action && !values.empty())
        action(std::move(values));
}
void ReviewGalleryModel::detail() {
    requireOwner();
    auto action = detail_;
    if (!action)
        return;
    if (session_->focusedKey().empty() && session_->visibleCount())
        session_->focusAt(0);
    if (!session_->focusedKey().empty())
        action({session_->focusedEntry()});
}
void ReviewGalleryModel::activate(std::string token) {
    requireOwner();
    const auto key = navigation_->keyForToken(token);
    const auto index = session_->indexForKey(key);
    if (index < 0)
        return;
    session_->focusAt(index);
    edit();
}
void ReviewGalleryModel::openDetail(std::string token) {
    requireOwner();
    const auto key = navigation_->keyForToken(token);
    const auto index = session_->indexForKey(key);
    if (index < 0) return;
    session_->focusAt(index);
    detail();
}
void ReviewGalleryModel::close() {
    if (!active_)
        return;
    requireOwner();
    active_ = false;
    observer_.reset();
    if (context_) {context_->close(); context_.reset();}
    navigation_->close();
    navigation_.reset();
    session_.reset();
    edit_={};editEligible_={};compare_={};export_={};detail_={};
    itemsByKey_.clear();
    (void)items.set({});
}

} // namespace dreamscapes::review

NativeReviewNavigationScope::NativeReviewNavigationScope()
    : value_(std::make_shared<dreamscapes::review::CollectionNavigation>()), state(value_->state) {}

NativeReviewGalleryBackend::NativeReviewGalleryBackend()
    : NativeReviewGalleryBackend(std::make_shared<dreamscapes::review::Session>()) {}
NativeReviewGalleryBackend::NativeReviewGalleryBackend(std::shared_ptr<dreamscapes::review::Session> source)
    : session(std::move(source)), model(Model::create(session)), comparison(dreamscapes::review::CompareModel::create()), items(model->items),
      resultSummary(model->resultSummary), selectionSummary(model->selectionSummary), canReview(model->canReview),
      hasSelection(model->hasSelection), canEdit(model->canEdit), canCompare(model->canCompare),
      canExport(model->canExport),comparing(comparison->visible),galleryVisible(comparison->galleryVisible),
      compareItems(comparison->items),syncLabel(comparison->syncLabel),compareWidth(comparison->width),
      compareHeight(comparison->height),compareContentWidth(comparison->contentWidth),compareGridHeight(comparison->gridHeight),
      compareCellWidth(comparison->cellWidth),compareCellHeight(comparison->cellHeight),comparePaneHeight(comparison->paneHeight) {
    const std::weak_ptr weakCompare=comparison;const std::weak_ptr weakGallery=model;
    comparison->setPaneFactory([weakCompare,weakGallery](const auto &item) {
        return dreamscapes::review::comparePaneSource(weakCompare,weakGallery,item);
    });
    model->setCompareHandler([weakCompare](auto entries) {if (auto c=weakCompare.lock()) (void)c->open(entries);});
}
