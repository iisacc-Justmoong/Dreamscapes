#include "App/Views/Result/ReviewSelection.h"
#include <iostream>
#include <stdexcept>

using dreamscapes::review::Selection;
using dreamscapes::review::State;
using dreamscapes::review::SelectionMode;

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    try {
        Selection state;
        state.synchronize({"a", "b", "c", "d"});
        state.select("b", state.order(), SelectionMode::Replace);
        state.toggleState({"b"}, State::Shortlisted);
        check(state.state("b") == State::Shortlisted, "shortlist did not apply");
        state.toggleState({"b"}, State::Rejected);
        check(state.state("b") == State::Rejected, "review states must be exclusive");
        state.toggleState({"b"}, State::Rejected);
        check(state.state("b") == State::Unreviewed, "toggle must restore unreviewed");
        state.setState({"b"}, State::Shortlisted);
        state.synchronize({"new", "d", "c", "b", "a", "e", "b", ""});
        check(state.order() == std::vector<std::string>({"a","b","c","d","new","e"}), "arrivals moved existing results");
        check(state.focusedKey() == "b" && state.selected("b"), "arrivals lost focus or selection");
        check(state.state("b") == State::Shortlisted, "arrivals lost review state");
        // The visible filter is a projection; a hidden selection stays selected.
        state.select("d", {"c", "d", "e"}, SelectionMode::Toggle);
        check(state.selectedKeys() == std::vector<std::string>({"b","d"}), "filtered selection lost hidden result");
        state.select("e", {"c", "d", "e"}, SelectionMode::Range);
        check(state.selectedKeys() == std::vector<std::string>({"d","e"}), "range must use visible order");
        state.select("a", state.order(), SelectionMode::Toggle);
        state.select("c", state.order(), SelectionMode::AddRange);
        check(state.selectedKeys() == std::vector<std::string>({"a","b","c","d","e"}), "additive range replaced selection");
        state.setState(state.selectedKeys(), State::Rejected);
        check(state.state("new") == State::Unreviewed && state.state("a") == State::Rejected, "bulk review changed other results");
        state.synchronize({"new", "b", "e"});
        check(state.focusedKey() == "b", "removed focus must fall back to a surviving result");
        check(state.selectedKeys() == std::vector<std::string>({"b","e"}), "removed results remained selected");
        state.clearSelection();
        check(state.selectedKeys().empty() && state.focusedKey() == "b", "selection clear changed focus");
        state.focus("new");
        state.select("e", state.order(), SelectionMode::Range);
        check(state.selectedKeys() == std::vector<std::string>({"new","e"}), "range did not start from plain focused result");
        state.synchronize({});
        check(state.order().empty() && state.focusedKey().empty(), "new storage session retained stale state");
        std::cout << "Review selection core contracts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
