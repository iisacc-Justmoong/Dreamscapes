#include "App/Views/Result/NativeReviewNavigation.h"
#include <LVUI/NativeVirtualCollection.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using dreamscapes::review::ReviewNavigation;
using dreamscapes::review::Session;
using dreamscapes::review::State;
using dreamscapes::vm::Object;
using dreamscapes::vm::Value;

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
Value entry(int index, std::string suffix = {}) {
    return Value(Object{{"id", Value(std::to_string(index))},
                        {"documentSource", Value("file:///review/" + std::to_string(index) + suffix + ".iisc")},
                        {"mediaType", Value("Image")}});
}
void navigation() {
    auto session = std::make_shared<Session>();
    std::vector<Value> entries;
    for (int i = 0; i < 1000; ++i)
        entries.push_back(entry(i));
    session->replaceEntries(entries);
    session->selectAt(5);
    session->selectAt(7, false, true);
    auto binding = ReviewNavigation::create(session);
    const auto fifth = binding->tokenForKey(session->keyAt(5));
    const auto seventh = binding->tokenForKey(session->keyAt(7));
    require(!fifth.empty() && !seventh.empty() && binding->navigation().is_selected(fifth) &&
                binding->navigation().is_selected(seventh),
            "Existing review selection was not reflected in native navigation");
    session->setFilter("rejected");
    require(binding->visibleTokens().empty() && session->hiddenSelectedCount() == 2 &&
                binding->navigation().is_selected(fifth) && binding->navigation().is_selected(seventh),
            "Filtering discarded a hidden selection");
    session->setFilter("all");
    int notifications = 0;
    auto observer = std::make_shared<dreamscapes::review::ChangePort>(
        [&](const dreamscapes::review::Change &) { ++notifications; });
    require(bool(session->observe(observer)), "Product change observer was rejected");
    auto source = lvui::native::virtual_collection_source(binding->visibleTokens(),
                                                          [](const auto &token, std::size_t) { return token; });
    binding->navigation().focus_at(*source, 10);
    binding->navigation().focus_at(*source, 13, {.range = true});
    require(session->selectedCount() == 4 && session->focusedIndex() == 13 && notifications == 2,
            "Native range selection did not reach the product session");
    session->setReview(State::Shortlisted);
    require(session->shortlistedCount() == 4 && session->entries() == entries,
            "Review state changed original result data");
    entries.push_back(entry(1000));
    session->replaceEntries(entries);
    require(session->focusedIndex() == 13 && session->selectedCount() == 4 &&
                binding->tokenForKey(session->keyAt(5)) == fifth,
            "A new result changed selection, focus or an existing view identity");
}
void retirementAndLongKeys() {
    auto session = std::make_shared<Session>();
    bool wrongThreadRejected = false;
    std::thread worker([&] {
        try {
            (void)ReviewNavigation::create(session);
        } catch (const std::runtime_error &) {
            wrongThreadRejected = true;
        }
    });
    worker.join();
    require(wrongThreadRejected, "A review session was bound from a foreign thread");
    const auto longEntry = entry(1, std::string(3000, 'x'));
    session->replaceEntries({entry(0), longEntry});
    auto binding = ReviewNavigation::create(session);
    const auto longKey = session->keyAt(1);
    const auto token = binding->tokenForKey(longKey);
    require(!token.empty() && token.size() < 1024 && binding->keyForToken(token) == longKey,
            "A long original source key was truncated or exposed as a widget identity");
    binding->navigation().set_state({.current_key = token, .anchor_key = token, .selected_keys = {token}});
    require(session->selectedCount() == 1 && session->focusedKey() == longKey,
            "Native state restoration did not select the original result");
    session->setFilter("rejected");
    require(binding->visibleTokens().empty() && session->hiddenSelectedCount() == 1,
            "Hidden selection was not retained before result removal");
    session->replaceEntries({entry(0)});
    require(session->selectedCount() == 0 && binding->keyForToken(token).empty() &&
                binding->navigation().state.get().selected_keys.empty(),
            "Removing a result retained stale navigation selection");
    session->replaceEntries({entry(0), longEntry});
    session->setFilter("all");
    const auto replacement = binding->tokenForKey(session->keyAt(1));
    require(replacement != token && !binding->navigation().is_selected(replacement),
            "A removed result inherited its retired view identity or selection");
    binding->navigation().set_state({.current_key = token, .anchor_key = token, .selected_keys = {token}});
    require(session->selectedCount() == 0 && !binding->navigation().is_selected(token),
            "A late event from a retired row changed product selection");
    binding->close();
    session->selectAt(0);
    require(session->selectedCount() == 1, "Retiring the UI binding retired its product session");
}
void closeDuringNotification() {
    auto session = std::make_shared<Session>();
    session->replaceEntries({entry(0), entry(1)});
    auto binding = ReviewNavigation::create(session);
    auto source = lvui::native::virtual_collection_source(binding->visibleTokens(),
                                                           [](const auto &token, std::size_t) { return token; });
    auto &navigation = binding->navigation();
    auto retiring = navigation.state.observe([&](const auto &, std::uint64_t) { binding->close(); });
    navigation.focus_at(*source, 1);
    session->selectAt(0);
    require(session->selectedCount() == 1 && session->focusedIndex() == 0,
            "Closing the native view during notification damaged its session");
}
void sharedNavigationLifetime() {
    auto session = std::make_shared<Session>();
    session->replaceEntries({entry(0), entry(1)});
    auto navigation = std::make_shared<lvui::native::CollectionNavigation>();
    auto binding = ReviewNavigation::bind(session, navigation);
    const auto target = binding->tokenForKey(session->keyAt(1));
    navigation->set_state({.current_key = target, .anchor_key = target, .selected_keys = {target}});
    require(session->focusedIndex() == 1 && session->selectedCount() == 1,
            "A shared LVUI navigation object did not update its review session");
    binding->close();
    navigation->set_current({});
    require(session->focusedIndex() == 1 && session->selectedCount() == 1,
            "Retiring a binding retained its session observer");
}
} // namespace

int main() {
    try {
        navigation();
        retirementAndLongKeys();
        closeDuringNotification();
        sharedNavigationLifetime();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Native review navigation preserves product selection, focus and source identity\n";
}
