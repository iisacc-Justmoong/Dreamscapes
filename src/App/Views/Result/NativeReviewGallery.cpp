#include "NativeReviewGallery.h"
#include <stdexcept>
#include <utility>

namespace dreamscapes::review {

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

void ReviewGalleryModel::bindNavigation(std::shared_ptr<lvui::native::CollectionNavigation> navigation) {
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
    }
    if (item->source != row.thumbnail) {
        (void)item->thumbnail.set({});
        item->source = row.thumbnail;
    }
    (void)item->title.set(row.title);
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

void ReviewGalleryModel::updateActions() {
    (void)resultSummary.set(std::to_string(session_->totalCount()) + " results");
    (void)selectionSummary.set(std::to_string(session_->selectedCount()) + " selected");
    const auto action = session_->actionEntries();
    (void)canReview.set(!action.empty());
    (void)hasSelection.set(session_->selectedCount() != 0);
    (void)canEdit.set(static_cast<bool>(edit_) && Session::validImages(action));
    (void)canCompare.set(static_cast<bool>(compare_) && Session::validImages(session_->selectedEntries(), 2, 4));
    (void)canExport.set(static_cast<bool>(export_) && !action.empty());
}

void ReviewGalleryModel::setEditHandler(Action handler) {
    requireOwner();
    edit_ = std::move(handler);
    updateActions();
}
void ReviewGalleryModel::setCompareHandler(Action handler) {
    requireOwner();
    compare_ = std::move(handler);
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
void ReviewGalleryModel::edit() {
    requireOwner();
    auto action = edit_;
    auto values = session_->actionEntries();
    if (action && Session::validImages(values))
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
void ReviewGalleryModel::close() {
    if (!active_)
        return;
    requireOwner();
    active_ = false;
    observer_.reset();
    navigation_->close();
    navigation_.reset();
    session_.reset();
    itemsByKey_.clear();
    (void)items.set({});
}

} // namespace dreamscapes::review

NativeReviewNavigationScope::NativeReviewNavigationScope()
    : value_(std::make_shared<lvui::native::CollectionNavigation>()), state(value_->state) {}

NativeReviewGalleryBackend::NativeReviewGalleryBackend()
    : session(std::make_shared<dreamscapes::review::Session>()), model(Model::create(session)), items(model->items),
      resultSummary(model->resultSummary), selectionSummary(model->selectionSummary), canReview(model->canReview),
      hasSelection(model->hasSelection), canEdit(model->canEdit), canCompare(model->canCompare),
      canExport(model->canExport) {}
