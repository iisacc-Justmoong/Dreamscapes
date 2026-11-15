#include "App/Views/Result/NativeReviewInspector.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace dreamscapes;
namespace {
void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
vm::Value image(std::string id, std::string seed) {
    return vm::Value(vm::Object{{"id", vm::Value(id)}, {"mediaType", vm::Value("Image")},
        {"imageSource", vm::Value("file:///preview/" + id + ".png")},
        {"documentSource", vm::Value("file:///original/" + id + ".iisc")},
        {"seed", vm::Value(seed)}, {"width", vm::Value(1024)}, {"height", vm::Value(768)}});
}
const review::InspectorField &field(const std::vector<review::InspectorField> &fields, const char *name) {
    const auto found = std::ranges::find_if(fields, [&](const auto &item) { return item.label == name; });
    if (found == fields.end()) throw std::runtime_error("An inspector field is missing");
    return *found;
}
void exercise() {
    auto session = std::make_shared<review::Session>();
    auto model = review::ReviewInspectorModel::create(session);
    check(model->fields.get().empty() && !model->canExpand.get() && !model->canSave.get(),
          "An empty inspector exposed result actions");
    const auto first = image("a", "17"), second = image("b", "29");
    session->replaceEntries({first, second});
    session->focusAt(0);
    check(model->fields.get().size() == 9 && field(model->fields.get(), "Seed").value == "17" &&
          field(model->fields.get(), "Model").value == "Unavailable" &&
          model->previewSource.get() == "file:///preview/a.png" &&
          model->originalSource.get() == "file:///original/a.iisc",
          "The inspector invented or dropped the focused result's metadata");
    check(!model->canExpand.get() && !model->canSave.get(), "Actions were enabled without handlers");
    vm::Value expanded, saved;
    model->setExpandHandler([&](vm::Value value) { expanded = std::move(value); });
    model->setSaveHandler([&](vm::Value value) { saved = std::move(value); });
    check(model->canExpand.get() && model->canSave.get(), "Valid handlers were not made available");
    model->expand(); model->save();
    check(expanded == first && saved == first, "Inspector actions did not preserve the original result");
    session->selectAt(0);
    session->selectAt(1, false, true);
    const auto &seed = field(model->fields.get(), "Seed");
    check(seed.mixed && seed.values == std::vector<std::string>({"17", "29"}) &&
          model->selectionSummary.get() == "2 selected", "Mixed selections lost their actual values");
    session->focusAt(1);
    model->expand();
    check(expanded == second && model->previewSource.get() == "file:///preview/b.png",
          "The focused preview or action did not follow focus");
    const std::string prompt(100000, 'p');
    const vm::Value clip(vm::Object{{"id", vm::Value("video")}, {"mediaType", vm::Value("Video")},
        {"imageSource", vm::Value("file:///preview/video.png")},
        {"mediaSource", vm::Value("file:///original/video.mp4")},
        {"documentSource", vm::Value("file:///original/video.iisc")}, {"prompt", vm::Value(prompt)}});
    auto observer = model->previewSource.observe([](const auto &, std::uint64_t) { throw std::runtime_error("observer"); });
    bool reported = false;
    try { session->replaceEntries({clip}); } catch (const std::runtime_error &) { reported = true; }
    check(reported && model->video.get() && model->mediaSource.get() == "file:///original/video.mp4" &&
          model->originalSource.get() == "file:///original/video.iisc" &&
          field(model->fields.get(), "Prompt").value == prompt && model->canSave.get(),
          "A failed subscriber prevented the other video fields or actions from updating");
    observer.reset();
    model->save();
    check(saved == clip, "Video save did not receive the unchanged source result");
    session->replaceEntries({vm::Value(vm::Object{{"id", vm::Value("empty")},
        {"containerId", vm::Value("container")}, {"key", vm::Value("source-free")}})});
    session->focusAt(0);
    check(!model->canExpand.get() && !model->canSave.get(), "A result without a source exposed actions");
    model->expand(); model->save();
    check(expanded == second && saved == clip, "A source-free result invoked an action");
    bool wrongThread = false;
    std::thread other([&] { try { model->setSaveHandler({}); } catch (const std::logic_error &) { wrongThread = true; } });
    other.join();
    check(wrongThread, "The inspector accepted an action from another thread");
    model->close();
    session->replaceEntries({first});
    check(!model->active() && !model->canExpand.get() && !model->canSave.get(),
          "A retired inspector was reactivated by a later session update");
}
}
int main() {
    try { exercise(); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Native review inspector preserves actual metadata and action targets\n";
}
