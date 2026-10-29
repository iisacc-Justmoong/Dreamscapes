#include "App/Views/Result/NativeReviewSession.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace dreamscapes::review;
using dreamscapes::vm::Array;
using dreamscapes::vm::Object;
using dreamscapes::vm::Value;
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
Value entry(int i, std::string type = "Image") {
    return Value(Object{{"id", Value(std::to_string(i))},
                        {"documentSource", Value("file:///review/" + std::to_string(i) + ".iisc")},
                        {"mediaType", Value(std::move(type))},
                        {"seed", Value(i)},
                        {"prompt", Value("Prompt " + std::to_string(i))}});
}
const InspectorRow &row(const std::vector<InspectorRow> &rows, const std::string &label) {
    for (const auto &value : rows)
        if (value.label == label)
            return value;
    throw std::runtime_error("missing inspector row: " + label);
}
void arrivalsAndFilters() {
    Session s;
    std::vector<Value> values{entry(0), entry(1), entry(2), entry(3)};
    s.replaceEntries(values);
    s.selectAt(1);
    s.selectAt(3, false, true);
    const auto selected = s.selectedEntries();
    const auto focused = s.focusedKey();
    s.toggleReview(State::Shortlisted);
    s.setFilter("rejected");
    require(s.visibleCount() == 0 && s.selectedCount() == 2 && s.hiddenSelectedCount() == 2,
            "filter lost hidden selection");
    require(s.focusedKey() == focused && s.selectedEntries() == selected, "filter changed focus or metadata");
    s.setFilter("all");
    values.insert(values.begin(), entry(9));
    values.push_back(entry(10));
    s.replaceEntries(values);
    require(s.visibleCount() == 6 && s.entryAt(0) == entry(0) && s.entryAt(4) == entry(9), "arrival order changed");
    require(s.focusedKey() == focused && s.shortlistedCount() == 2 && s.selectedEntries() == selected,
            "arrival lost review state");
    require(!dreamscapes::vm::has(s.entryAt(0), "reviewState"), "review state contaminated original metadata");
    s.clearSelection();
    require(s.selectedCount() == 0 && s.focusedKey() == focused, "clear lost focused item");
}
void rangesAndNotifications() {
    Session s;
    std::vector<Value> values;
    for (int i = 0; i < 1000; ++i)
        values.push_back(entry(i));
    s.replaceEntries(values);
    s.setSort("seed");
    s.selectAt(500);
    s.setReview(State::Shortlisted);
    std::vector<Change> changes;
    auto sink = std::make_shared<ChangePort>([&](const Change &change) { changes.push_back(change); });
    require(bool(s.observe(sink)), "observer admission failed");
    s.focusAt(501);
    require(changes.size() == 1 && changes[0].projection == ProjectionChange::None && !changes[0].entriesChanged &&
                changes[0].rows.size() == 2,
            "focus invalidated unrelated rows");
    require(s.selectedCount() == 1 && s.shortlistedCount() == 1, "focus changed counters");
    changes.clear();
    values.push_back(entry(1000));
    s.replaceEntries(values);
    require(changes.size() == 1 && changes[0].projection == ProjectionChange::Append && changes[0].entriesChanged &&
                changes[0].previousCount == 1000 && s.focusedIndex() == 501,
            "arrival reset the list or focus");
    s.setFilter("rejected");
    require(s.hiddenSelectedCount() == 1, "hidden selection was lost");
    s.setFilter("all");
    s.selectAt(0);
    s.selectAt(2, true, false);
    require(s.selectedCount() == 3, "range selection differs");
}
void identityAndSource() {
    Session s;
    auto a = entry(1);
    auto fields = dreamscapes::vm::object(a);
    fields.erase("documentSource");
    fields["imageSource"] = Value("image://preview/1?v=one");
    a = Value(fields);
    s.replaceEntries({a});
    s.selectAt(0);
    s.setReview(State::Shortlisted);
    const auto key = s.focusedKey();
    fields["imageSource"] = Value("image://preview/1?v=two");
    a = Value(fields);
    s.replaceEntries({a});
    require(s.focusedKey() == key && s.selectedCount() == 1 && s.shortlistedCount() == 1,
            "preview revision changed identity");
    auto video = entry(2, "Video");
    fields = dreamscapes::vm::object(video);
    fields["mediaSource"] = Value("file:///review/Video outputs/2.mp4");
    video = Value(fields);
    s.replaceEntries({a, video});
    s.focusSource("file:///review/Video%20outputs/2.mp4?version=3");
    require(s.focusedEntry() == video && s.selectedCount() == 1, "encoded source did not focus the original video");
    require(Session::sourceIdentity("file:///a%2Fb") != Session::sourceIdentity("file:///a/b"),
            "reserved URI bytes collapsed");
    require(Session::sourceIdentity("file:///space%20percent%.png") == "file:///space%2520percent%25.png",
            "a malformed escape changed a literal file name");
}
void inspectorAndKeyboard() {
    Session s;
    auto a = entry(0), b = entry(1, "Video");
    auto v = dreamscapes::vm::object(a);
    v["width"] = Value(1024);
    v["height"] = Value(768);
    v["startedAt"] = Value("2026-10-24T00:00:00.000Z");
    v["finishedAt"] = Value("2026-10-24T00:00:01.250Z");
    v["advancedParameters"] = Value(Object{{"sampler", Value("euler")}});
    a = Value(v);
    s.replaceEntries({a, b});
    s.selectAt(0);
    auto rows = s.inspectorRows();
    require(row(rows, "Model").value == "Unavailable" && row(rows, "Seed").value == "0",
            "missing or zero metadata changed");
    require(row(rows, "Resolution").value == "1024 × 768" && row(rows, "Generation time").value == "1.25 s",
            "inspector values differ");
    require(row(rows, "Generation settings").value.find("euler") != std::string::npos, "generation parameters missing");
    s.selectAt(1, false, true);
    rows = s.inspectorRows();
    require(row(rows, "Resolution").mixed && s.selectedImages().size() == 1,
            "mixed inspector or image filtering differs");
    const auto ignored = s.key({.key = dreamscapes::vm::ReviewKey::reject}, true);
    require(!ignored.accepted && s.rejectedCount() == 0, "text input consumed a review shortcut");
    auto command = s.key({.key = dreamscapes::vm::ReviewKey::shortlist}, false);
    require(command.accepted && s.shortlistedCount() == 2, "keyboard review target differs");
    require(!s.validImages(s.selectedEntries(), 2, 4), "video accepted for image comparison");
}
void navigationSnapshot() {
    Session session;
    const std::vector<Value> entries{entry(0), entry(1), entry(2), entry(3)};
    session.replaceEntries(entries);
    session.selectAt(1);
    session.selectAt(2, false, true);
    const auto initial = session.navigationState();
    require(initial.currentKey == session.keyAt(2) && initial.anchorKey == session.keyAt(2) &&
                initial.selectedKeys.size() == 2,
            "initial native navigation snapshot is incomplete");
    session.setFilter("rejected");
    require(session.navigationState() == initial && session.hiddenSelectedCount() == 2,
            "filtering erased a hidden navigation selection");
    session.setFilter("all");
    std::vector<Change> changes;
    auto observer = std::make_shared<ChangePort>([&](const Change &change) { changes.push_back(change); });
    require(bool(session.observe(observer)), "navigation observer was rejected");
    const NavigationState desired{session.keyAt(0), session.keyAt(1), {session.keyAt(2), session.keyAt(0)}};
    const auto restored = session.restoreNavigation(desired);
    require(restored && *restored && session.selectedCount() == 2 && session.focusedIndex() == 0 &&
                changes.size() == 1 && changes[0].projection == ProjectionChange::None,
            "native navigation snapshot was not applied once");
    const auto before = session.navigationState();
    const auto repeated = session.restoreNavigation(desired);
    require(repeated && !*repeated && changes.size() == 1, "identical navigation snapshot caused a change");
    auto invalid = desired;
    invalid.selectedKeys.push_back("missing-result");
    const auto rejected = session.restoreNavigation(invalid);
    require(!rejected && session.navigationState() == before && changes.size() == 1,
            "invalid navigation snapshot changed selection or focus");
    session.focusAt(0);
    require(session.navigationState().anchorKey == session.keyAt(0) && changes.size() == 2,
            "changing only the range anchor failed to notify native navigation");
    require(bool(session.restoreNavigation(desired)), "range anchor could not be restored");
    session.selectAt(2, true, false);
    require(session.selectedCount() == 2 && session.isSelected(entry(1)) && session.isSelected(entry(2)),
            "restored anchor was not used for range selection");
    auto focusOnly = session.navigationState();
    focusOnly.currentKey = session.keyAt(3);
    changes.clear();
    require(bool(session.restoreNavigation(focusOnly)) && changes.size() == 1 &&
                changes[0].projection == ProjectionChange::None && changes[0].rows.size() == 2 &&
                changes[0].rows[0].roles == Focused && changes[0].rows[1].roles == Focused,
            "focus-only navigation invalidated unrelated rows");
    require(session.entries() == entries, "navigation changed result metadata");
}
void lifetimeAndLargeSeeds() {
    auto owner = std::make_unique<Session>();
    owner->replaceEntries({entry(0), entry(1)});
    bool called = false;
    auto sink = std::make_shared<ChangePort>([&](const Change &) {
        called = true;
        owner.reset();
    });
    require(bool(owner->observe(sink)), "observer admission failed");
    owner->focusAt(1);
    require(called && !owner, "notification owner did not retire safely");
    Session session;
    bool rejected = false;
    std::thread other([&] {
        try {
            session.selectAll();
        } catch (const std::logic_error &) {
            rejected = true;
        }
    });
    other.join();
    require(rejected && session.selectedCount() == 0, "foreign-thread mutation was accepted");
    auto flagged = dreamscapes::vm::object(entry(50));
    flagged.erase("documentSource");
    flagged["imageSource"] = Value("file:///review/image.png");
    flagged["isImage"] = Value(0);
    require(!Session::validImages({Value(flagged)}), "numeric false admitted as an image");
    flagged["isImage"] = Value("true");
    require(Session::validImages({Value(flagged)}), "string true rejected as an image");
    require(Session::imageEligible("Image", false, true) && !Session::imageEligible("Video", true, true) &&
                !Session::imageEligible("Canvas", true, true),
            "image eligibility changed document fallback or media-type exclusions");
    auto a = dreamscapes::vm::object(entry(0)), b = dreamscapes::vm::object(entry(1));
    a["seed"] = Value(std::int64_t{9007199254740993LL});
    b["seed"] = Value(std::int64_t{9007199254740992LL});
    session.replaceEntries({Value(a), Value(b)});
    session.setSort("seed");
    require(session.entryAt(0) == Value(b), "large seeds lost integer precision in sorting");
    auto unsignedSeed = dreamscapes::vm::object(entry(2));
    unsignedSeed["seed"] = Value(std::uint64_t{9223372036854775808ULL});
    session.replaceEntries({Value(a), Value(b), Value(unsignedSeed)});
    require(session.entryAt(0) == Value(unsignedSeed),
            "unsigned seed sort lost the existing signed conversion contract");
    session.replaceEntries({Value(a), Value(b)});
    const auto first = session.rowAt(0);
    require(first && first->entry == Value(b) && first->ordinal == 2 && first->title == "Image · 02",
            "row presentation lost its source ordinal");
    require(!session.rowAt(-1) && !session.rowAt(2), "invalid row became a result");
    int delivered = 0;
    auto bad = std::make_shared<ChangePort>([](const Change &) { throw std::runtime_error("observer failure"); });
    auto good = std::make_shared<ChangePort>([&](const Change &) { ++delivered; });
    require(bool(session.observe(bad)) && bool(session.observe(good)), "observer registration failed");
    bool surfaced = false;
    try {
        session.focusAt(0);
    } catch (const std::runtime_error &) {
        surfaced = true;
    }
    require(surfaced && delivered == 1 && session.focusedIndex() == 0,
            "observer failure prevented delivery or rolled back committed focus");
    bad.reset();
    session.focusAt(1);
    require(session.focusedIndex() == 1, "observer failure left session busy");
}
} // namespace
int main() {
    try {
        arrivalsAndFilters();
        rangesAndNotifications();
        identityAndSource();
        inspectorAndKeyboard();
        navigationSnapshot();
        lifetimeAndLargeSeeds();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Native review session: identity, selection, projection, inspector and keyboard passed\n";
}
