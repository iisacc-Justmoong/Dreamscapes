#include "App/Views/Home/NativeHomeRecentFiles.h"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace dreamscapes;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
vm::Value file(std::string id, std::string name, bool local = true) {
    return vm::Value(vm::Object{{"id", vm::Value(std::move(id))}, {"name", vm::Value(std::move(name))},
        {"available", vm::Value(local)}, {"opaque", vm::Value(vm::Object{{"id", vm::Value(42)}})}});
}
void exercise() {
    auto model = home::HomeRecentFilesModel::create();
    check(model->rows.get().empty() && model->emptyMessage.get() == "No recent files" &&
        model->selectionSummary.get() == "No file selected", "empty state was not projected");
    check(bool(model->setAvailabilityMessage("Storage disconnected")) && model->emptyMessage.get()=="Storage disconnected",
          "an unavailable store looked empty");
    check(bool(model->setAvailabilityMessage({})) && model->emptyMessage.get()=="No recent files",
          "a successful empty store retained an error");
    std::string selection, openedToken, selectedFilter; vm::Value opened(vm::Object{}); int browsed = 0;
    model->setHandlers({.select = [&](std::string value) { selection = std::move(value); },
        .open = [&](std::string token, vm::Value value) { openedToken = std::move(token); opened = std::move(value); },
        .browse = [&] { ++browsed; }, .filter = [&](std::string value) { selectedFilter = std::move(value); }});
    const auto first = file("first", "first.iisc");
    const auto second = file("second", "second.png", false);
    const auto third = file("third", "third.mp4");
    auto image = vm::object(second); image["isImage"] = vm::Value(true);
    const vm::Value imageFile(std::move(image));
    const auto items = vm::Value(vm::Array{first, imageFile, third});
    check(bool(model->setFiles(items, {"file:///first", "file:///second", "file:///third"}, false)), "file snapshot rejected");
    check(model->rows.get().size() == 3 && model->selectedKey.get() == "first" && selection == "first" &&
        model->countSummary.get() == "3 files" && model->selectionSummary.get() == "1 selected · first.iisc",
        "file count or initial selection did not match Home");
    auto rows = model->rows.get(); const auto firstToken = rows[0].token, secondToken = rows[1].token;
    check(rows[0].selected && !rows[1].selected && model->tokenForKey("first") == firstToken &&
        rows[0].preview == "file:///first" && rows[1].state == "Not downloaded" &&
        rows[1].description == "Image", "real preview or file facts were lost");
    check(bool(model->selectToken(secondToken)) && model->selectedKey.get() == "second" && selection == "second",
        "row selection did not update shared selection");
    check(!model->rows.get()[0].selected && model->rows.get()[1].selected, "selected row did not update");
    check(bool(model->setFilter("Canvas")) && model->rows.get().size() == 1 && model->selectedKey.get() == "second",
        "filtering incorrectly changed the selected file");
    check(bool(model->setFilter("All types")), "all-types filter rejected");
    rows = model->rows.get();
    check(rows[0].token == firstToken && rows[1].token == secondToken, "filtering changed row identities");
    check(bool(model->setFiles(vm::Value(vm::Array{imageFile, third, first}),
        {"file:///second", "file:///third", "file:///first"}, false)), "reordered snapshot rejected");
    rows = model->rows.get();
    check(rows[0].token == secondToken && rows[2].token == firstToken && model->selectedKey.get() == "second",
        "reordering lost selection or identities");
    check(bool(model->openToken(secondToken)) && opened == imageFile && openedToken == secondToken &&
        model->sourceIndexForToken(secondToken) == 0, "open lost the remote file identity or object");
    model->browse(); check(browsed == 1, "view all command was not delivered");
    check(bool(model->setFiles(vm::Value(vm::Array{first, third}), {"", ""}, false)) &&
        model->selectedKey.get() == "first" && selection == "first", "removed selection was not reconciled");
    check(!model->openToken(secondToken) && opened == imageFile, "retired row opened a file");
    check(!model->sourceIndexForToken(secondToken), "retired file identity remained accessible");
    check(bool(model->filterAt(3)) && selectedFilter == "Video" && model->filter.get() == "Video", "user filter action was not delivered");
    check(bool(model->setFilter("All types")) && selectedFilter == "Video", "external filter update emitted a user action");
    check(!model->setFilter("invented") && model->filter.get() == "All types", "unsupported filter changed view state");
    model->sizeChanged(600, 400);
    check(model->viewportWidth.get() == 600 && model->viewportHeight.get() == 400 && model->showFilter.get() &&
        model->showUpdated.get() && model->showSpecifications.get(), "wide columns or bounds were lost");
    model->sizeChanged(450, 400);
    check(model->showFilter.get() && model->showUpdated.get() && !model->showSpecifications.get(), "medium column visibility failed");
    model->sizeChanged(400, 400);
    check(!model->showFilter.get() && !model->showUpdated.get() && !model->showSpecifications.get(), "narrow column visibility failed");
    check(!model->setFiles(vm::Value(vm::Object{}), {}, false), "non-list snapshot was accepted");
    check(bool(model->setFiles(vm::Value(vm::Array{}), {}, true)) && model->showEmpty.get() && model->emptyMessage.get() == "Reading Society…" &&
        model->selectedKey.get().empty(), "loading/empty state did not clear selection");
    ii::core::ErrorCode other = ii::core::ErrorCode::failed;
    std::thread foreign([&] { const auto result = model->setFilter("Canvas"); if (!result) other = result.error().code; });
    foreign.join(); check(other == ii::core::ErrorCode::wrong_executor, "foreign thread changed file list state");
    model->close(); model->browse(); check(browsed == 1, "retired view kept its browse command");

    auto observed = home::HomeRecentFilesModel::create();
    ii::core::ErrorCode reentrant = ii::core::ErrorCode::failed;
    auto subscription = observed->selectedKey.observe([&](const auto &, std::uint64_t) {
        const auto result = observed->setFilter("Image");
        if (!result) reentrant = result.error().code;
    });
    check(bool(observed->setFiles(vm::Value(vm::Array{first}), {""}, false)) &&
        reentrant == ii::core::ErrorCode::busy && observed->filter.get() == "All types" &&
        observed->rows.get().size() == 1, "selection publication allowed a reentrant file-list mutation");
    check(!observed->setFiles(vm::Value(vm::Array{first, third}), {"one"}, false) &&
        observed->rows.get().size() == 1 && observed->selectedKey.get() == "first",
        "invalid preview alignment changed the current snapshot");
    auto throwing = home::HomeRecentFilesModel::create();
    auto throwingSubscription = throwing->selectedKey.observe([](const auto &, std::uint64_t) {
        throw std::runtime_error("observer failed");
    });
    const auto failed = throwing->setFiles(vm::Value(vm::Array{first}), {""}, false);
    check(!failed && throwing->rows.get().size() == 1 && throwing->countSummary.get() == "1 files" &&
        throwing->selectionSummary.get() == "1 selected · first.iisc", "observer failure left a partially published list");
    auto retiring = home::HomeRecentFilesModel::create();
    int retiredCommands = 0;
    retiring->setHandlers({.browse = [&] { ++retiredCommands; }});
    auto retirement = retiring->selectedKey.observe([&](const auto &, std::uint64_t) { retiring->setHandlers({}); });
    check(bool(retiring->setFiles(vm::Value(vm::Array{first}), {""}, false)), "session retirement interrupted list publication");
    retiring->browse(); check(retiredCommands == 0, "retired session kept a live command");
}
}
int main() {
    try { exercise(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
