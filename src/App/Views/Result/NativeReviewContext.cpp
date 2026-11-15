#include "NativeReviewContext.h"
#include "App/ViewModels/Native/ViewValue.h"
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dreamscapes::review {
namespace {
bool hasSource(const vm::Value &value) {
    for (const auto key : std::array{"documentSource","mediaSource","imageSource"})
        if (!vm::text(vm::get(value,key)).empty()) return true;
    return false;
}
bool isVideo(const vm::Value &value) {return vm::text(vm::get(value,"mediaType"))=="Video";}
}
ReviewContextModel::ReviewContextModel(std::shared_ptr<Session> session) : session_(std::move(session)) {
    if (!session_) throw std::invalid_argument("Review context requires a session");
    strings_={[](std::size_t n){return "Shortlist · "+std::to_string(n)+" results";},
              [](std::size_t n){return "Reject · "+std::to_string(n)+" results";},
              [](std::size_t n){return "Reset review · "+std::to_string(n)+" results";},
              [](std::size_t n){return "Compare · "+std::to_string(n)+" images";},
              [](std::size_t n){return "Edit · "+std::to_string(n)+" images";},
              "Save current to File","Save current to Photos"};
}
std::shared_ptr<ReviewContextModel> ReviewContextModel::create(std::shared_ptr<Session> session) {
    return std::shared_ptr<ReviewContextModel>(new ReviewContextModel(std::move(session)));
}
ReviewContextModel::~ReviewContextModel() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
void ReviewContextModel::requireOwner() const {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Review context requires its owner thread");
    if (!active_) throw std::logic_error("Review context is closed");
}
void ReviewContextModel::setStrings(ReviewContextStrings value) {
    requireOwner();
    if (!value.shortlist || !value.reject || !value.reset || !value.compare || !value.edit)
        throw std::invalid_argument("Review context requires complete labels");
    const auto revision=stringsRevision.get();
    if (revision==std::numeric_limits<std::uint64_t>::max()) throw std::length_error("Review context label revision exhausted");
    strings_=std::move(value); (void)stringsRevision.set(revision+1);
}
std::string ReviewContextModel::label(ReviewContextAction action) const {
    requireOwner(); (void)stringsRevision.get(); const auto count=context.get().targets.size();
    switch (action) {
    case ReviewContextAction::shortlist: return strings_.shortlist(count);
    case ReviewContextAction::reject: return strings_.reject(count);
    case ReviewContextAction::reset: return strings_.reset(count);
    case ReviewContextAction::compare: return strings_.compare(count);
    case ReviewContextAction::edit: return strings_.edit(count);
    case ReviewContextAction::saveFile: return strings_.saveFile;
    case ReviewContextAction::savePhotos: return strings_.savePhotos;
    }
    return {};
}
bool ReviewContextModel::enabled(ReviewContextAction action) const {
    requireOwner(); const auto &value=context.get();
    switch (action) {
    case ReviewContextAction::shortlist: case ReviewContextAction::reject: case ReviewContextAction::reset: return !value.targets.empty();
    case ReviewContextAction::compare: return value.canCompare;
    case ReviewContextAction::edit: return value.canEdit;
    case ReviewContextAction::saveFile: return value.canSaveFile;
    case ReviewContextAction::savePhotos: return value.canSavePhotos;
    }
    return false;
}
void ReviewContextModel::refreshAvailability() {
    auto value=context.get();
    value.canCompare=bool(compare_) && Session::validImages(value.targets,2,4);
    value.canEdit=canEdit(value.targets);
    value.canSaveFile=canSaveFile(value.current);
    value.canSavePhotos=bool(savePhotos_) && hasSource(value.current) && !isVideo(value.current);
    (void)context.set(std::move(value));
}
bool ReviewContextModel::canEdit(const std::vector<vm::Value> &targets) const {
    if (!edit_ || !Session::validImages(targets)) return false;
    try {return !editEligible_ || editEligible_(targets);} catch (...) {return false;}
}
bool ReviewContextModel::canSaveFile(const vm::Value &current) const {
    if (!saveFile_) return false;
    try {return saveFileEligible_?saveFileEligible_({current}):hasSource(current);} catch (...) {return false;}
}
void ReviewContextModel::setEditHandler(Action value,Eligibility eligible) {
    requireOwner();edit_=std::move(value);editEligible_=std::move(eligible);refreshAvailability();
}
void ReviewContextModel::setCompareHandler(Action value) {requireOwner(); compare_=std::move(value); refreshAvailability();}
void ReviewContextModel::setSaveFileHandler(Action value,Eligibility eligible) {
    requireOwner();saveFile_=std::move(value);saveFileEligible_=std::move(eligible);refreshAvailability();
}
void ReviewContextModel::setSavePhotosHandler(Action value) {requireOwner(); savePhotos_=std::move(value); refreshAvailability();}
bool ReviewContextModel::open(std::string_view key) {
    requireOwner();
    const auto keepAlive=shared_from_this(); const std::string id(key);
    const auto index=session_->indexForKey(id); if (index<0) return false;
    session_->focusAt(index);
    if (!active_ || !session_ || session_->focusedKey()!=id) return false;
    if (nextGeneration_==std::numeric_limits<std::uint64_t>::max()) throw std::length_error("Review context generation exhausted");
    ReviewContextState value;
    value.generation=++nextGeneration_;
    value.current=session_->focusedEntry(); value.targets=session_->contextEntries();
    value.canCompare=bool(compare_) && Session::validImages(value.targets,2,4);
    value.canEdit=canEdit(value.targets);
    value.canSaveFile=canSaveFile(value.current);
    value.canSavePhotos=bool(savePhotos_) && hasSource(value.current) && !isVideo(value.current);
    (void)context.set(std::move(value));
    return active_;
}
bool ReviewContextModel::invoke(ReviewContextAction action) {
    requireOwner();
    const auto keepAlive=shared_from_this(); const auto snapshot=context.get();
    if (snapshot.targets.empty()) return false;
    switch (action) {
    case ReviewContextAction::shortlist: session_->markEntries(snapshot.targets,State::Shortlisted); return true;
    case ReviewContextAction::reject: session_->markEntries(snapshot.targets,State::Rejected); return true;
    case ReviewContextAction::reset: session_->markEntries(snapshot.targets,State::Unreviewed); return true;
    case ReviewContextAction::compare:
        if (snapshot.canCompare && compare_) {auto handler=compare_; handler(snapshot.targets); return true;} return false;
    case ReviewContextAction::edit:
        if (snapshot.canEdit && canEdit(snapshot.targets)) {auto handler=edit_; handler(snapshot.targets); return true;} return false;
    case ReviewContextAction::saveFile:
        if (snapshot.canSaveFile && canSaveFile(snapshot.current)) {auto handler=saveFile_; handler({snapshot.current}); return true;} return false;
    case ReviewContextAction::savePhotos:
        if (snapshot.canSavePhotos && savePhotos_) {auto handler=savePhotos_; handler({snapshot.current}); return true;} return false;
    }
    return false;
}
void ReviewContextModel::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Review context requires its owner thread");
    if (!active_) return;
    active_=false; edit_={}; editEligible_={}; compare_={}; saveFile_={}; saveFileEligible_={}; savePhotos_={}; session_.reset();
    (void)context.set({});
}
} // namespace dreamscapes::review
