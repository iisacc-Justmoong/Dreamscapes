#pragma once

#include <iiGUI>
#include <functional>
#include <utility>

namespace dreamscapes::home {
// QuickGenerate hosts its controls in separate native views. Keep their
// popup lifetimes exclusive across these sibling widget trees.
class HomeComposerPopups final {
  public:
    void toggle(ii::gui::Widget popup, ii::gui::Widget anchor, const std::function<bool()> &prepare = {}) {
        if (popup.is_null() || switching_) return;
        const auto button = anchor.interaction();
        if (!button || !button->visible || !button->enabled) return;
        struct EndSwitch final { bool &active; ~EndSwitch() { active = false; } } reset{switching_};
        switching_ = true;
        if (active_ == popup) {
            const auto current = popup.interaction();
            if (current && current->opened) {
                if (popup.cancel_popup()) active_ = {};
                return;
            }
        }
        auto previous = std::exchange(active_, {});
        if (!previous.is_null() && previous != popup) {
            const auto closed = previous.cancel_popup();
            if (!closed && closed.error().code != ii::core::ErrorCode::closed) {
                active_ = previous;
                return;
            }
        }
        if (prepare && !prepare()) return;
        if (popup.open_for(anchor, 0, button->bounds.height)) active_ = popup;
    }

    void close(ii::gui::Widget popup) {
        const auto closed = popup.close_popup();
        if (active_ == popup && (closed || closed.error().code == ii::core::ErrorCode::closed)) active_ = {};
    }

  private:
    ii::gui::Widget active_;
    bool switching_ = false;
};
} // namespace dreamscapes::home
