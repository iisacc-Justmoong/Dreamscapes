#pragma once

#include "ReviewSelection.h"
#include "../../ViewModels/Native/ReviewCommands.h"
#include "../../ViewModels/Native/ViewValue.h"
#include <iiSDKCore/Core/Result/Result.h>
#include <iiSDKCore/Runtime/Query.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace dreamscapes::review {
// A projection change is separate from original result and document data.
enum class ProjectionChange { None, Append, Reset };
enum RowRole : unsigned { Metadata = 1, Selected = 2, Focused = 4, Review = 8 };
struct RowChange {
    std::string key;
    unsigned roles = 0;
};
struct Change {
    ProjectionChange projection = ProjectionChange::None;
    std::size_t previousCount = 0;
    bool entriesChanged = false;
    std::vector<RowChange> rows;
};
using ChangePort = ii::runtime::QueryPort<Change>;
struct InspectorRow {
    std::string label, value;
    bool mixed = false;
    std::vector<std::string> values;
};
struct Row {
    std::string key, mediaType, title;
    vm::Value entry, thumbnail;
    State review = State::Unreviewed;
    int ordinal = 0;
    bool selected = false, focused = false;
};
struct KeyResult {
    bool accepted = false;
    vm::ReviewAction action = vm::ReviewAction::ignored;
    int index = -1;
};
struct NavigationState {
    std::string currentKey, anchorKey;
    std::vector<std::string> selectedKeys;
    friend bool operator==(const NavigationState &, const NavigationState &) = default;
};

// Owned by the UI domain. Entries are detached iiSDK values; selection and
// review state are session-only and cannot write to a canvas or source file.
class Session final {
  public:
    Session();
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    static std::string sourceIdentity(std::string_view source);
    static std::string keyForEntry(const vm::Value &entry);
    static constexpr bool imageEligible(std::string_view mediaType, bool imageFlag, bool hasDocument) noexcept {
        return mediaType != "Video" && mediaType != "Canvas" && (imageFlag || hasDocument);
    }
    static bool validImages(const std::vector<vm::Value> &entries, int minimum = 1, int maximum = 1000);
    ii::core::Result<void> observe(std::shared_ptr<const ChangePort>);
    void replaceEntries(const std::vector<vm::Value> &);
    void setFilter(std::string_view);
    void setSort(std::string_view);
    void setJobFilter(std::string);
    void setSelectionMode(bool);
    void selectAt(int index, bool range = false, bool add = false);
    void focusAt(int index);
    void focusSource(std::string_view);
    void toggle(const vm::Value &);
    void selectAll();
    void clearSelection();
    void toggleReview(State);
    void setReview(State);
    void setEntryReview(std::string_view key, State);
    void markEntries(const std::vector<vm::Value> &, State);
    KeyResult key(vm::ReviewKeyInput input, bool textEditing);
    NavigationState navigationState() const;
    ii::core::Result<bool> restoreNavigation(NavigationState);

    std::size_t totalCount() const noexcept { return selection_.order().size(); }
    const std::vector<std::string> &allKeys() const noexcept { return selection_.order(); }
    std::size_t visibleCount() const noexcept { return visible_.size(); }
    std::size_t selectedCount() const noexcept { return selected_; }
    std::size_t shortlistedCount() const noexcept { return shortlisted_; }
    std::size_t rejectedCount() const noexcept { return rejected_; }
    std::size_t hiddenSelectedCount() const noexcept { return hiddenSelected_; }
    bool selectionMode() const noexcept { return selectionMode_; }
    const std::string &filter() const noexcept { return filter_; }
    const std::string &sort() const noexcept { return sort_; }
    const std::string &jobFilter() const noexcept { return jobFilter_; }
    const std::string &focusedKey() const noexcept { return selection_.focusedKey(); }
    int focusedIndex() const;
    std::string keyAt(int) const;
    int indexForKey(std::string_view) const;
    vm::Value entryAt(int) const;
    std::optional<Row> rowAt(int) const;
    vm::Value focusedEntry() const;
    State reviewState(std::string_view key) const;
    bool isSelected(const vm::Value &) const;
    std::vector<vm::Value> entries() const;
    std::vector<vm::Value> selectedEntries() const;
    std::vector<vm::Value> selectedImages() const;
    std::vector<vm::Value> actionEntries() const;
    std::vector<vm::Value> contextEntries() const;
    std::vector<InspectorRow> inspectorRows() const;

  private:
    struct RowState {
        State review;
        bool selected, focused;
    };
    struct Lifetime {
        bool alive = true, notifying = false;
    };
    void mutableOnOwner() const;
    void publish(Change);
    void rebuild(bool project, const std::vector<std::string> &metadataChanged = {}, bool entriesChanged = false);
    void publishFocus(const std::string &previous);
    std::vector<std::string> projection() const;
    std::vector<std::string> actionKeys() const;
    vm::Value entryFor(std::string_view) const;
    RowState rowState(const std::string &) const;
    bool reviewFiltered() const;

    std::thread::id owner_;
    Selection selection_;
    vm::ReviewCommands commands_;
    std::unordered_map<std::string, vm::Value> entries_;
    std::unordered_map<std::string, int> ordinals_, visibleIndices_;
    std::vector<std::string> visible_;
    std::vector<RowState> rowStates_;
    std::vector<std::weak_ptr<const ChangePort>> observers_;
    std::shared_ptr<Lifetime> lifetime_;
    std::string filter_ = "all", sort_ = "arrival", jobFilter_;
    std::size_t selected_ = 0, shortlisted_ = 0, rejected_ = 0, hiddenSelected_ = 0;
    bool selectionMode_ = false;
};
} // namespace dreamscapes::review
