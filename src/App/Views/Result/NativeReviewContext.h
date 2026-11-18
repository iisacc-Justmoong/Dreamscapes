#pragma once
#include "NativeReviewSession.h"
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dreamscapes::review {
enum class ReviewContextAction { shortlist, reject, reset, compare, edit, saveFile, savePhotos };
struct ReviewContextState final {
    std::vector<vm::Value> targets;
    vm::Value current;
    std::uint64_t generation=0;
    bool canCompare=false,canEdit=false,canSaveFile=false,canSavePhotos=false;
    friend bool operator==(const ReviewContextState &,const ReviewContextState &)=default;
};
struct ReviewContextStrings final {
    std::function<std::string(std::size_t)> shortlist,reject,reset,compare,edit;
    std::string saveFile,savePhotos;
};
// A menu captures its own target snapshot; it neither owns nor modifies files.
class ReviewContextModel final : public std::enable_shared_from_this<ReviewContextModel> {
  public:
    using Action=std::function<void(std::vector<vm::Value>)>;
    using Eligibility=std::function<bool(const std::vector<vm::Value> &)>;
    static std::shared_ptr<ReviewContextModel> create(std::shared_ptr<Session>);
    ~ReviewContextModel();
    ii::core::Observable<ReviewContextState> context;
    ii::core::Observable<std::uint64_t> stringsRevision{0};
    void setStrings(ReviewContextStrings);
    void setEditHandler(Action,Eligibility={});
    void setCompareHandler(Action);
    void setSaveFileHandler(Action,Eligibility={});
    void setSavePhotosHandler(Action);
    [[nodiscard]] bool open(std::string_view key);
    [[nodiscard]] bool invoke(ReviewContextAction);
    [[nodiscard]] std::string label(ReviewContextAction) const;
    [[nodiscard]] bool enabled(ReviewContextAction) const;
    void close();
    [[nodiscard]] bool active() const noexcept {return active_;}
  private:
    explicit ReviewContextModel(std::shared_ptr<Session>);
    void requireOwner() const;
    void refreshAvailability();
    [[nodiscard]] bool canEdit(const std::vector<vm::Value> &) const;
    [[nodiscard]] bool canSaveFile(const vm::Value &) const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<Session> session_;
    Action edit_,compare_,saveFile_,savePhotos_;
    Eligibility editEligible_,saveFileEligible_;
    ReviewContextStrings strings_;
    std::uint64_t nextGeneration_=0;
    bool active_=true;
};
} // namespace dreamscapes::review
