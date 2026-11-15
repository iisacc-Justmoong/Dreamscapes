#pragma once

#include "NativeReviewInspector.h"
#include <LVUI/CodeDocument.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dreamscapes::review {
struct InspectorViewStrings final {
    std::string heading, focused, preview, expand, save, unavailable, noResult;
    std::function<std::string(std::size_t)> selected;
    std::function<std::string(std::string_view)> field, job, original;
};
struct InspectorViewLayout final {
    double previewHeight, minimumFieldHeight, maximumFieldHeight, longFieldHeight;
};

class InspectorDisplayField final {
  public:
    using Ptr = std::shared_ptr<InspectorDisplayField>;
    InspectorDisplayField(std::string key, InspectorViewLayout);
    const std::string key;
    const std::shared_ptr<lvui::CodeMemoryDocument> document;
    ii::core::Observable<std::string> label, text;
    ii::core::Observable<bool> usesWrappedText{true};
    ii::core::Observable<double> height{1};
    ii::core::Observable<std::int64_t> revision{0};
    [[nodiscard]] ii::core::Result<void> update(std::string label, std::string text);
    [[nodiscard]] ii::core::Result<void> setMeasuredHeight(double);
    void wrappingFallbackChanged(bool);
    void close() noexcept { active_ = false; }
  private:
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_ = std::this_thread::get_id();
    InspectorViewLayout layout_;
    bool active_ = true;
};

// Presentation state for an LVUI view. It never writes to the result session.
class InspectorViewModel final : public std::enable_shared_from_this<InspectorViewModel> {
  public:
    static std::shared_ptr<InspectorViewModel> create(std::shared_ptr<ReviewInspectorModel>,
                                                       InspectorViewStrings, InspectorViewLayout);
    ~InspectorViewModel();
    InspectorViewModel(const InspectorViewModel &) = delete;
    InspectorViewModel &operator=(const InspectorViewModel &) = delete;
    [[nodiscard]] ii::core::Result<void> setViewport(double width, double height);
    void expand();
    void save();
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }

    const InspectorViewStrings strings;
    ii::core::Observable<std::vector<InspectorDisplayField::Ptr>> fields;
    ii::core::Observable<std::string> selectionSummary, previewSource, jobText, originalText;
    ii::core::Observable<bool> canExpand{false}, canSave{false}, jobVisible{false}, noPreview{true};
    ii::core::Observable<double> viewportWidth{1}, viewportHeight{1}, previewHeight{1};
  private:
    InspectorViewModel(std::shared_ptr<ReviewInspectorModel>, InspectorViewStrings, InspectorViewLayout);
    void start();
    void updateFields(const std::vector<InspectorField> &);
    void updateSelection(int);
    void updatePreview(const std::string &);
    void updateJob(const std::string &);
    void updateOriginal(const std::string &);
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<ReviewInspectorModel> inspector_;
    InspectorViewLayout layout_;
    std::vector<ii::core::ObservableSubscription> observations_;
    bool active_ = true;
};
} // namespace dreamscapes::review
