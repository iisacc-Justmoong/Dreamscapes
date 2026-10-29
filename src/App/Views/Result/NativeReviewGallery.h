#pragma once

#include "NativeReviewNavigation.h"
#include <iiGUI/Image.h>
#include <iiSDKCore/Core/Property/Observable.h>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dreamscapes::review {
struct ReviewGalleryItem final {
    std::string key, token;
    vm::Value source;
    ii::core::Observable<std::string> title, reviewLabel;
    ii::core::Observable<bool> selected{false}, focused{false};
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> thumbnail;
};

// Presentation only: a caller supplies real result values, preview images and
// edit/compare/export handlers. Missing handlers never fabricate an operation.
class ReviewGalleryModel final : public std::enable_shared_from_this<ReviewGalleryModel> {
  public:
    using Action = std::function<void(std::vector<vm::Value>)>;
    using Item = std::shared_ptr<ReviewGalleryItem>;
    static std::shared_ptr<ReviewGalleryModel> create(std::shared_ptr<Session> session);
    ~ReviewGalleryModel();
    ReviewGalleryModel(const ReviewGalleryModel &) = delete;
    ReviewGalleryModel &operator=(const ReviewGalleryModel &) = delete;

    ii::core::Observable<std::vector<Item>> items;
    ii::core::Observable<std::string> resultSummary, selectionSummary;
    ii::core::Observable<bool> canReview{false}, hasSelection{false};
    ii::core::Observable<bool> canEdit{false}, canCompare{false}, canExport{false};

    std::shared_ptr<Session> session() const;
    std::shared_ptr<ReviewNavigation> navigation() const;
    void bindNavigation(std::shared_ptr<lvui::native::CollectionNavigation> navigation);
    bool setThumbnail(std::string_view key, const vm::Value &expectedSource,
                      std::shared_ptr<const ii::gui::RasterImage> image);
    void setEditHandler(Action);
    void setCompareHandler(Action);
    void setExportHandler(Action);
    void setDetailHandler(Action);
    void edit();
    void compare();
    void exportResults();
    void detail();
    void activate(std::string token);
    void close();

  private:
    explicit ReviewGalleryModel(std::shared_ptr<Session> session);
    void requireOwner() const;
    void start();
    void update(const Change &change);
    void rebuildItems(const std::unordered_map<std::string, Item> *previous = nullptr);
    void updateItem(const Row &row);
    void updateActions();

    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<Session> session_;
    std::shared_ptr<ReviewNavigation> navigation_;
    std::shared_ptr<ChangePort> observer_;
    std::unordered_map<std::string, Item> itemsByKey_;
    Action edit_, compare_, export_, detail_;
    bool active_ = true;
};
} // namespace dreamscapes::review

// LVUI owns this scope before the gallery backend. Shared ownership allows the
// product binding to retire independently without invalidating another owner.
class NativeReviewNavigationScope final {
    std::shared_ptr<lvui::native::CollectionNavigation> value_;
    std::function<void()> detail_;

  public:
    lvui::native::CollectionNavigation::StateSource &state;
    NativeReviewNavigationScope();
    std::shared_ptr<lvui::native::CollectionNavigation> shared() const { return value_; }
    void configure(lvui::native::CollectionNavigationOptions options) {
        options.anchor_on_unselected_focus = true;
        value_->configure(options);
    }
    bool is_selected(std::string_view key) const { return value_->is_selected(key); }
    bool set_current(std::string key) { return value_->set_current(std::move(key)); }
    template <class... Args> auto focus_at(Args &&...args) { return value_->focus_at(std::forward<Args>(args)...); }
    void setDetailHandler(std::function<void()> handler) { detail_ = std::move(handler); }
    template <class Source, class... Args>
    auto command(const Source &source, lvui::native::CollectionCommand command, Args &&...args) {
        if (command == lvui::native::CollectionCommand::toggle) {
            if (detail_)
                detail_();
            return lvui::native::CollectionNavigationReply{.handled = true};
        }
        return value_->command(source, command, std::forward<Args>(args)...);
    }
};

class NativeReviewGalleryBackend final {
  public:
    using Model = dreamscapes::review::ReviewGalleryModel;
    std::shared_ptr<dreamscapes::review::Session> session;
    std::shared_ptr<Model> model;
    ii::core::Observable<std::vector<Model::Item>> &items;
    ii::core::Observable<std::string> &resultSummary, &selectionSummary;
    ii::core::Observable<bool> &canReview, &hasSelection, &canEdit, &canCompare, &canExport;
    NativeReviewGalleryBackend();
    void bindNavigation(std::shared_ptr<lvui::native::CollectionNavigation> value) {
        model->bindNavigation(std::move(value));
    }
    void showAll() { session->setFilter("all"); }
    void showShortlisted() { session->setFilter("shortlisted"); }
    void showUnreviewed() { session->setFilter("unreviewed"); }
    void showRejected() { session->setFilter("rejected"); }
    void showImages() { session->setFilter("images"); }
    void showVideos() { session->setFilter("videos"); }
    void shortlist() { session->toggleReview(dreamscapes::review::State::Shortlisted); }
    void reject() { session->toggleReview(dreamscapes::review::State::Rejected); }
    void clearSelection() { session->clearSelection(); }
    void activate(std::string token) { model->activate(std::move(token)); }
    void edit() { model->edit(); }
    void compare() { model->compare(); }
    void exportResults() { model->exportResults(); }
};
