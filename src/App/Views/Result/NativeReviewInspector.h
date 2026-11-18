#pragma once

#include "NativeReviewSession.h"
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace dreamscapes::review {
struct InspectorField final {
    std::string label, value;
    bool mixed = false;
    std::vector<std::string> values;
    friend bool operator==(const InspectorField &, const InspectorField &) = default;
};

class ReviewInspectorModel final : public std::enable_shared_from_this<ReviewInspectorModel> {
  public:
    using Action = std::function<void(vm::Value)>;
    static std::shared_ptr<ReviewInspectorModel> create(std::shared_ptr<Session>);
    ~ReviewInspectorModel();
    ReviewInspectorModel(const ReviewInspectorModel &) = delete;
    ReviewInspectorModel &operator=(const ReviewInspectorModel &) = delete;

    ii::core::Observable<std::vector<InspectorField>> fields;
    ii::core::Observable<std::string> selectionSummary, previewSource, mediaSource, originalSource, jobId;
    ii::core::Observable<int> selectionCount{0};
    ii::core::Observable<bool> video{false}, canExpand{false}, canSave{false};
    bool active() const noexcept { return active_; }
    void setExpandHandler(Action);
    void setSaveHandler(Action);
    void expand();
    void save();
    void close();

  private:
    explicit ReviewInspectorModel(std::shared_ptr<Session>);
    void requireOwner() const;
    void start();
    void refresh();
    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<Session> session_;
    std::shared_ptr<ChangePort> observer_;
    vm::Value focused_;
    Action expand_, save_;
    bool active_ = true, updating_ = false, hasSource_ = false;
};
} // namespace dreamscapes::review
