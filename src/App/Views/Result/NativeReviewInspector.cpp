#include "NativeReviewInspector.h"
#include <exception>
#include <stdexcept>
#include <utility>

namespace dreamscapes::review {
namespace {
template<class Observable, class T>
void publish(Observable &observable, T value, std::exception_ptr &failure) {
    try { observable.set(std::move(value)); }
    catch (...) { if (!failure) failure = std::current_exception(); }
}
}
std::shared_ptr<ReviewInspectorModel> ReviewInspectorModel::create(std::shared_ptr<Session> session) {
    auto result = std::shared_ptr<ReviewInspectorModel>(new ReviewInspectorModel(std::move(session)));
    result->start();
    return result;
}
ReviewInspectorModel::ReviewInspectorModel(std::shared_ptr<Session> session) : session_(std::move(session)) {
    if (!session_) throw std::invalid_argument("A review inspector requires a session");
}
ReviewInspectorModel::~ReviewInspectorModel() {
    if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {}
}
void ReviewInspectorModel::requireOwner() const {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Review inspector requires its owner thread");
    if (!active_) throw std::logic_error("Review inspector is closed");
    if (updating_) throw std::logic_error("Review inspector update is already in progress");
}
void ReviewInspectorModel::start() {
    requireOwner();
    const std::weak_ptr<ReviewInspectorModel> weak = shared_from_this();
    observer_ = std::make_shared<ChangePort>([weak](const Change &) {
        if (auto owner = weak.lock(); owner && owner->active_) owner->refresh();
    });
    if (const auto result = session_->observe(observer_); !result) throw std::runtime_error(result.error().message);
    refresh();
}
void ReviewInspectorModel::refresh() {
    requireOwner();
    const auto keepAlive = shared_from_this();
    struct Reset final { bool &value; ~Reset() { value = false; } } reset{updating_};
    updating_ = true;
    std::vector<InspectorField> nextFields;
    for (const auto &row : session_->inspectorRows())
        nextFields.push_back({row.label, row.value, row.mixed, row.values});
    auto next = session_->focusedEntry();
    auto preview = vm::text(vm::get(next, "imageSource"));
    auto media = vm::text(vm::get(next, "mediaSource"));
    auto original = vm::text(vm::get(next, "documentSource"));
    auto job = vm::text(vm::get(next, "id"));
    const bool available = !preview.empty() || !media.empty() || !original.empty();
    const bool isVideo = vm::text(vm::get(next, "mediaType")) == "Video";
    const auto originalLabel = !original.empty() ? original : !media.empty() ? media : std::string("Unavailable");
    const auto count = session_->selectedCount();
    const auto summary = count > 1 ? std::to_string(count) + " selected" : std::string("Focused result");
    focused_ = std::move(next);
    hasSource_ = available;
    std::exception_ptr failure;
    publish(fields, std::move(nextFields), failure);
    publish(selectionCount, static_cast<int>(count), failure);
    publish(selectionSummary, summary, failure);
    publish(previewSource, std::move(preview), failure);
    publish(mediaSource, std::move(media), failure);
    publish(originalSource, originalLabel, failure);
    publish(jobId, std::move(job), failure);
    publish(video, isVideo, failure);
    publish(canExpand, available && bool(expand_), failure);
    publish(canSave, available && bool(save_), failure);
    if (failure) std::rethrow_exception(failure);
}
void ReviewInspectorModel::setExpandHandler(Action handler) {
    requireOwner();
    const auto keepAlive = shared_from_this();
    expand_ = std::move(handler);
    refresh();
}
void ReviewInspectorModel::setSaveHandler(Action handler) {
    requireOwner();
    const auto keepAlive = shared_from_this();
    save_ = std::move(handler);
    refresh();
}
void ReviewInspectorModel::expand() {
    requireOwner();
    const auto keepAlive = shared_from_this();
    auto action = expand_;
    auto value = focused_;
    if (hasSource_ && action) action(std::move(value));
}
void ReviewInspectorModel::save() {
    requireOwner();
    const auto keepAlive = shared_from_this();
    auto action = save_;
    auto value = focused_;
    if (hasSource_ && action) action(std::move(value));
}
void ReviewInspectorModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Review inspector requires its owner thread");
    if (!active_) return;
    if (updating_) throw std::logic_error("Review inspector update is already in progress");
    active_ = false;
    observer_.reset();
    expand_ = {}; save_ = {}; hasSource_ = false;
    focused_ = {};
    std::exception_ptr failure;
    publish(canExpand, false, failure);
    publish(canSave, false, failure);
    if (failure) std::rethrow_exception(failure);
}
} // namespace dreamscapes::review
