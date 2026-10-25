#include "App/ViewModels/Native/ViewValue.h"
#include "App/ViewModels/Native/VideoTimeline.h"
#include "App/ViewModels/Native/GenerationPresentation.h"
#include "App/ViewModels/Native/GenerationDraft.h"
#include "App/ViewModels/Native/EditorToolState.h"
#include <iiJson/Json.h>
#include <fstream>
#include <chrono>
#include <iiSDKCore/Runtime/Query.h>
#include <iostream>
#include <stdexcept>

using namespace dreamscapes::vm;
namespace {
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
void values() {
    const Value source(Object{{"layers", Value(Array{Value(Object{{"x", Value(3)}})})}});
    const auto next = withPath(source, "layers[0].x", Value(7));
    require(number(*pathValue(source, "layers[0].x")) == 3, "draft edits changed original snapshot");
    require(number(*pathValue(next, "layers[0].x")) == 7, "nested array edit lost");
    require(!pathValue(next, "layers[1].x"), "missing member became null");
    const auto combined = nested(Value(Object{{"a", Value(Array{Value(1), Value(2)})}}),
        Value(Object{{"a[1]", Value(9)}}));
    require(number(*pathValue(combined, "a[1]")) == 9, "overlay precedence lost");
    require(nested(flatten(combined)) == combined, "flatten/nested roundtrip");
    const Value field(Object{{"min", Value(0)}, {"max", Value(100)}, {"integer", Value(true)}, {"unit", Value("%")}});
    require(parseNumber(field, "42%", true).valid, "valid numeric input rejected");
    require(!parseNumber(field, "42x", true).valid, "partial numeric input accepted");
    require(!parseNumber(field, "101%", true).valid, "out of range accepted");
    require(!parseNumber(field, "1.5%", true).valid, "fraction accepted for integer");
    require(!parseNumber(field, "NaN", true).valid, "NaN accepted");
    const auto rows = buttonRows(100, Value(Array{Value(70), Value(70), Value(20)}), 3, 22, 4, true);
    require(array(get(rows, "items")).size() == 3 && number(get(rows, "height")) == 48, "button wrapping changed");
}
void timeline() {
    VideoTimeline model;
    auto send = [&](TimelineAction action, std::vector<Value> args = {}) {
        auto result = model.commands().send({action, std::move(args)});
        require(result.has_value(), "native command delivery failed");
        return result->returned;
    };
    require(array(get(model.presentation(), "tracks")).empty(), "default tracks were hardcoded");
    send(TimelineAction::reset, {Value(120), Value(24)});
    const auto before = model.snapshot();
    send(TimelineAction::seek, {Value(55)});
    require(!boolean(get(model.presentation(), "canUndo")), "seek created edit history");
    require(array(get(model.snapshot(), "tracks")).empty(), "seek created track");
    auto audio = send(TimelineAction::addTrack, {Value("Audio"), Value("Music"), Value(-1)});
    auto layers = send(TimelineAction::addTrack, {Value("Shots"), Value("Scenes"), Value(-1)});
    require(text(get(array(get(model.presentation(), "tracks"))[0], "type")) == "Shots", "track type ordering changed");
    require(boolean(send(TimelineAction::addShot, {layers})), "shot not added");
    send(TimelineAction::seek, {Value(60)});
    require(boolean(send(TimelineAction::split)), "split failed");
    require(array(get(model.presentation(), "shots")).size() == 2, "split lost a shot");
    const auto beforeInvalid = model.snapshot();
    require(!boolean(send(TimelineAction::resizeComposition, {Value(3), Value(24)})), "invalid duration accepted");
    require(model.snapshot() == beforeInvalid, "failed resize partially changed composition");
    send(TimelineAction::undo);
    require(array(get(model.presentation(), "shots")).size() == 1, "undo failed");
    send(TimelineAction::redo);
    require(array(get(model.presentation(), "shots")).size() == 2, "redo failed");
    require(boolean(send(TimelineAction::addKey, {Value("file:///a.iisc"), Value(5)})), "native image key not added");
    require(boolean(send(TimelineAction::addKey, {Value("file:///b.iisc"), Value(6)})), "second key not added");
    const auto collision = model.snapshot();
    require(!boolean(send(TimelineAction::resizeComposition, {Value(4), Value(24)})), "key collision accepted");
    require(model.snapshot() == collision, "key collision changed snapshot");
    const auto recipe = model.recipeShots("test prompt");
    require(array(recipe).size() == 2 && !array(get(array(recipe)[0], "conditions")).empty(), "recipe lost native conditions");
}
void drafts() {
    int resized = 0; bool allow = true;
    VideoDraft draft([&](int frames, double fps) { ++resized; return allow && frames > 0 && fps > 0; });
    auto edit = [&](std::string key, Value value) { const auto result = draft.edits().send({std::move(key), std::move(value)}); require(result.has_value(), "draft query failed"); return *result; };
    require(!edit("width", Value(33)), "non aligned width accepted");
    require(number(get(draft.values(), "width")) == 1024, "invalid width overwrote accepted draft");
    require(edit("aspectRatio", Value("9:16")), "valid aspect ratio rejected");
    require(number(get(draft.values(), "width")) == 576 && number(get(draft.values(), "height")) == 1024, "aspect ratio dimensions lost");
    require(edit("duration", Value(3)) && resized == 1, "duration did not use composition interface");
    const auto old = draft.values(); allow = false;
    require(!edit("fps", Value(30)) && draft.values() == old, "failed timeline resize partially published draft");
    require(edit("camera1", Value("pan-left")) && edit("camera2", Value("pan-right")) && draft.invalid(), "camera conflict not guarded");
    require(edit("camera2", Value("none")), "camera edit rejected");
    draft.restoreComposition(120, 24);
    require(number(get(draft.values(), "duration")) == 5 && !has(draft.invalidInputs(), "fps"), "undo composition restoration lost draft values");
    InputValidity validity;
    auto record = [&](ValidityEdit request) { return validity.commands().send(request); };
    require(record({"control.a", false}).has_value(), "validity query failed"); record({"width", false});
    const auto fields = record({{}, true, false, true});
    require(fields && has(*fields, "width") && !has(*fields, "control.a"), "collection refresh cleared unrelated input error");
    record({{}, true, true}); require(!validity.invalid(), "reset did not clear draft validity");
}
void generation() {
    GenerationPresenter presenter;
    GenerationSnapshot input;
    input.jobs = Value(Array{Value(Object{{"id", Value("old")}, {"state", Value("running")}}), Value(Object{{"id", Value("new")}, {"state", Value("queued")}})});
    input.requestIds = Value(Array{Value("new")}); input.currentResult = Value(Object{{"imageSource", Value("file:///manual.png")}});
    auto result = presenter.queries().send(input); require(result.has_value(), "generation projection query failed");
    require(boolean(get(*result, "generationPending")) && object(get(*result, "activeGeneration")).empty(), "unrelated job became submission owner");
    require(text(get(get(*result, "reviewActiveJob"), "id")) == "old", "running review job missing");
    input.requestIds = Value(Array{}); input.results = Value(Array{}); result = presenter.queries().send(input);
    require(array(get(*result, "reviewResults")).size() == 1, "manual result disappeared on unrelated queue update");
    input.results = Value(Array{input.currentResult}); result = presenter.queries().send(input);
    require(array(get(*result, "reviewResults")).size() == 1, "manual result duplicated");
}
void editorTools() {
    std::map<std::string, Value, std::less<>> resources;
    for (auto name : {"tools", "graphic", "elements", "compact", "field-groups"}) {
        std::ifstream file(std::string(DREAMSCAPES_TEST_SOURCE_DIR) + "/resources/viewmodels/" + name + ".json");
        const std::string bytes((std::istreambuf_iterator<char>(file)), {}); ii::json::Limits limits; limits.items = 500000; auto parsed = ii::json::parse(bytes, limits);
        if (!parsed) throw std::runtime_error(std::string("catalog ") + name + ": " + parsed.error().message);
        resources[name] = std::move(*parsed);
    }
    ViewCatalog catalog(std::move(resources)); EditorToolState state(catalog);
    auto command = [&](ToolAction action, std::vector<Value> args) { auto result = state.commands().send({action, std::move(args)}); require(result.has_value(), "tool command failed"); return *result; };
    const auto before = state.values();
    const auto view = command(ToolAction::write, {Value("elementsViewId"), Value("stroke")});
    require(state.values() == before && view.edits.empty(), "view selection edited accepted tool fields");
    command(ToolAction::write, {Value("toolId"), Value("color")});
    auto edits = command(ToolAction::setGraphicValues, {Value("developColor"), Value(Object{{"points", Value(Array{Value(Object{{"hue", Value(20)}})})}, {"saturation", Value(30)}})});
    require(edits.edits.size() == 3, "graphic update omitted operation or nested parameters");
    command(ToolAction::setGraphicValue, {Value("developColor"), Value("points[0].hue"), Value(40)});
    require(number(*pathValue(get(state.values(), "graphicParameters"), "points[0].hue")) == 40, "native graphic nested member did not apply");
    command(ToolAction::replaceGraphicCollection, {Value("developColor"), Value("points"), Value(Array{Value(Object{{"hue", Value(60)}})})});
    require(number(*pathValue(get(state.values(), "graphicParameters"), "points[0].hue")) == 60, "stale indexed edit survived collection replacement");
    command(ToolAction::write, {Value("toolId"), Value("elements")});
    command(ToolAction::acceptedSelection, {Value(Object{{"selectedLayerId", Value("shape-1")}, {"elementSelectionCount", Value(1)}, {"selectedElementSettings", Value(Object{{"selector", Value("Ellipse")}, {"field-0", Value(Array{Value(123), Value(456)})}})}}), Value(true)});
    require(array(get(state.values(), "field-0")).size() == 2, "selected shape dimensions lost");
    require(number(array(get(state.values(), "field-0"))[0]) == 123, "dummy dimensions replaced actual selected shape");
}
}
int main() {
    try { values(); timeline(); drafts(); generation(); editorTools(); std::cout << "native view value, timeline, draft, generation and editor contracts passed\n"; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
