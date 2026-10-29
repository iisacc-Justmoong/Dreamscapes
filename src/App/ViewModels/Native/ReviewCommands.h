#pragma once
#include <algorithm>
#include <iiSDKCore/Runtime/Query.h>

namespace dreamscapes::vm {
enum class ReviewKey { other, escape, all, shortlist, reject, compare, space, enter, left, right, up, down, home, end };
enum class ReviewAction { ignored, back, clear, selectAll, shortlist, reject, compare, detail, edit, focus };
struct ReviewKeyInput {
    ReviewKey key = ReviewKey::other;
    bool command = false, alt = false, shift = false, overlay = false, detail = false, compare = false;
    bool selected = false, selectionMode = false;
    int count = 0, focusedIndex = -1, columns = 1;
};
struct ReviewKeyPlan {
    ReviewAction action = ReviewAction::ignored;
    int index = -1;
};
inline ReviewKeyPlan reviewKey(const ReviewKeyInput &input) {
    if (input.key == ReviewKey::escape)
        return {input.overlay                           ? ReviewAction::back
                : input.selected || input.selectionMode ? ReviewAction::clear
                                                        : ReviewAction::back};
    if (input.command && input.key == ReviewKey::all)
        return {ReviewAction::selectAll};
    if (input.command || input.alt)
        return {};
    switch (input.key) {
    case ReviewKey::shortlist:
        return {ReviewAction::shortlist};
    case ReviewKey::reject:
        return {ReviewAction::reject};
    case ReviewKey::compare:
        return {ReviewAction::compare};
    case ReviewKey::space:
        return {input.detail ? ReviewAction::back : ReviewAction::detail};
    case ReviewKey::enter:
        return {ReviewAction::edit};
    default:
        break;
    }
    if (input.detail || input.compare || input.count <= 0)
        return {};
    auto index = std::max(0, input.focusedIndex);
    switch (input.key) {
    case ReviewKey::left:
        --index;
        break;
    case ReviewKey::right:
        ++index;
        break;
    case ReviewKey::up:
        index -= std::max(1, input.columns);
        break;
    case ReviewKey::down:
        index += std::max(1, input.columns);
        break;
    case ReviewKey::home:
        index = 0;
        break;
    case ReviewKey::end:
        index = input.count - 1;
        break;
    default:
        return {};
    }
    return {ReviewAction::focus, std::clamp(index, 0, input.count - 1)};
}
class ReviewCommands final {
  public:
    ReviewCommands() : keys_([](const ReviewKeyInput &input) { return reviewKey(input); }) {}
    const ii::runtime::QueryPort<ReviewKeyInput, ReviewKeyPlan> &keys() const {
        return keys_;
    }

  private:
    ii::runtime::QueryPort<ReviewKeyInput, ReviewKeyPlan> keys_;
};
} // namespace dreamscapes::vm
