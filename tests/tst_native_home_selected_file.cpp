#include "App/Views/Home/NativeHomeSelectedFile.h"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace dreamscapes;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
vm::Value localFile() {
    return vm::Value(vm::Object{{"id", vm::Value("one")}, {"name", vm::Value("canvas.iisc")},
        {"relativePath", vm::Value("Campaigns/canvas.iisc")}, {"available", vm::Value(true)},
        {"dateText", vm::Value("Yesterday")}, {"metadata1", vm::Value("18 MB")},
        {"opaque", vm::Value(vm::Object{{"source", vm::Value("must remain intact")}})}});
}
void exercise() {
    auto owner = home::HomeSelectedFileModel::create();
    auto &model = *owner;
    check(!model.hasFile.get() && !model.canOpen.get() && model.name.get() == "Select a recent file",
          "empty state was not represented");
    check(!model.setMetadata(0, vm::Value(vm::Object{{"width", vm::Value(10)}})),
          "metadata was accepted without a selected file");
    int opened = 0;
    model.setOpenHandler([&] { ++opened; });
    model.open(); check(opened == 0 && !model.canOpen.get(), "empty selection opened a file");
    const auto one = localFile();
    const auto firstRevision = model.setFile(one, "file:///canvas-preview.png");
    check(bool(firstRevision), "local file was rejected");
    check(model.hasFile.get() && model.canOpen.get() && model.name.get() == "canvas.iisc" &&
          model.path.get() == "Campaigns/canvas.iisc" && model.previewSource.get() == "file:///canvas-preview.png" &&
          model.openLabel.get() == "Open in editor" && model.dimensions.get() == "Unavailable",
          "local file projection changed or fabricated metadata");
    const auto details = vm::Value(vm::Object{{"width", vm::Value(1024)}, {"height", vm::Value(768)},
        {"layerCount", vm::Value(4)}, {"pageCount", vm::Value(1)}, {"colorSpace", vm::Value("ARGB 8-bit")}});
    check(bool(model.setMetadata(*firstRevision, details)), "matching metadata was rejected");
    check(model.dimensions.get() == "1024 × 768 px" && model.structure.get() == "1 pages · 4 layers" &&
          model.color.get() == "ARGB 8-bit", "document metadata did not update the selected view");
    model.open(); check(opened == 1 && model.file() == one, "open changed the original file");
    const auto remote = vm::Value(vm::Object{{"id", vm::Value("two")}, {"name", vm::Value("remote.iisc")},
        {"available", vm::Value(false)}, {"metadata2", vm::Value("Society / Remote")}});
    check(bool(model.setFile(remote, "")), "remote file was rejected");
    check(model.openLabel.get() == "Download and open" && model.path.get() == "Society / Remote" &&
          model.dimensions.get() == "Unavailable" && model.color.get() == "Unavailable",
          "selection change retained stale metadata or lost remote state");
    const auto stale = model.setMetadata(*firstRevision, details);
    check(!stale && stale.error().code == ii::core::ErrorCode::cancelled && model.dimensions.get() == "Unavailable",
          "a stale metadata reply changed the current file");
    model.open(); check(opened == 2, "remote selection did not dispatch open/download");
    const auto back = model.setFile(one, "file:///canvas-preview.png");
    check(bool(back) && *back != *firstRevision, "reselection did not get a new metadata generation");
    check(!model.setMetadata(*firstRevision, details) && model.dimensions.get() == "Unavailable",
          "an old reply was accepted after returning to the same file");
    model.setOpenHandler({});
    model.open(); check(opened == 2 && !model.canOpen.get(), "detached handler remained active");
    ii::core::ErrorCode other = ii::core::ErrorCode::failed;
    std::thread foreign([&] { const auto r = model.setFile(one, ""); if (!r) other = r.error().code; });
    foreign.join(); check(other == ii::core::ErrorCode::wrong_executor, "foreign thread changed view state");
}
} // namespace

int main() {
    try { exercise(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
