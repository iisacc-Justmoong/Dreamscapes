#include "App/ViewModels/Native/CanvasDraft.h"
#include "App/ViewModels/Native/EditorCommands.h"
#include "App/ViewModels/Native/EditorToolState.h"
#include "App/ViewModels/Native/GenerationDraft.h"
#include "App/ViewModels/Native/GenerationPresentation.h"
#include "App/ViewModels/Native/GraphicDraft.h"
#include "App/ViewModels/Native/ImageSelection.h"
#include "App/ViewModels/Native/PhotoPresentation.h"
#include "App/ViewModels/Native/ProjectionRows.h"
#include "App/ViewModels/Native/ReviewCommands.h"
#include "App/ViewModels/Native/StorageProjection.h"
#include "App/ViewModels/Native/VideoTimeline.h"
#include "App/ViewModels/Native/ViewValue.h"
#include "App/ViewModels/Native/ViewProjection.h"
#include "App/ViewModels/Native/TimelinePlayback.h"
#include "App/ViewModels/Native/BackgroundPalette.h"
#include "App/ViewModels/Native/NumberInputs.h"
#include <chrono>
#include <fstream>
#include <future>
#include <iiJson/Json.h>
#include <iiSDKCore/Runtime/Query.h>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace dreamscapes::vm;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void values() {
    const Value source(Object{{"layers", Value(Array{Value(Object{{"x", Value(3)}})})}});
    const auto next = withPath(source, "layers[0].x", Value(7));
    require(number(*pathValue(source, "layers[0].x")) == 3, "draft edits changed original snapshot");
    require(number(*pathValue(next, "layers[0].x")) == 7, "nested array edit lost");
    require(!pathValue(next, "layers[1].x"), "missing member became null");
    const auto combined =
        nested(Value(Object{{"a", Value(Array{Value(1), Value(2)})}}), Value(Object{{"a[1]", Value(9)}}));
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
    require(text(send(TimelineAction::addTrack, {Value("Camera"), Value("Unsupported")})).empty(),
            "unsupported track returned a nonempty identity");
    send(TimelineAction::reset, {Value(120), Value(24)});
    const auto empty = model.snapshot();
    const auto invalidFps = model.commands().send({TimelineAction::resizeComposition, {Value(24), Value()}, true});
    require(invalidFps && !boolean(invalidFps->returned), "nonfinite FPS accepted");
    require(model.snapshot() == empty && model.undoDepth() == 0, "rejected FPS changed composition/history");
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
    send(TimelineAction::selectAllShots, {Value(true)});
    require(array(get(model.presentation(), "selectedIds")).size() == 2, "select all lost a shot");
    require(boolean(send(TimelineAction::setRangeText, {Value("markIn"), Value("F005")})), "frame input rejected");
    const auto beforeRangeError = model.snapshot();
    require(!boolean(send(TimelineAction::setRangeText, {Value("markOut"), Value("F004")})), "inverted range accepted");
    require(!boolean(send(TimelineAction::setRangeText, {Value("markIn"), Value("3junk")})),
            "partial frame input accepted");
    require(model.snapshot() == beforeRangeError, "invalid range mutated timeline");
    require(model.shotIndex("shot-1") == 0 && model.shotIndex("missing") == -1, "shot index lost stable identity");
    const auto beforeInvalid = model.snapshot();
    require(!boolean(send(TimelineAction::resizeComposition, {Value(3), Value(24)})), "invalid duration accepted");
    require(model.snapshot() == beforeInvalid, "failed resize partially changed composition");
    send(TimelineAction::undo);
    require(array(get(model.presentation(), "shots")).size() == 1, "undo failed");
    send(TimelineAction::redo);
    require(array(get(model.presentation(), "shots")).size() == 2, "redo failed");
    require(boolean(send(TimelineAction::addKey, {Value("file:///a.iisc"), Value(5)})), "native image key not added");
    require(boolean(send(TimelineAction::addKey, {Value("file:///b.iisc"), Value(6)})), "second key not added");
    require(model.shotReference("shot-1") == "file:///a.iisc", "shot reference did not use matching frame");
    const auto collision = model.snapshot();
    require(!boolean(send(TimelineAction::resizeComposition, {Value(4), Value(24)})), "key collision accepted");
    require(model.snapshot() == collision, "key collision changed snapshot");
    send(TimelineAction::copyKey);
    require(!text(get(model.presentation(), "clipboardKey")).empty(), "copy key did not enable paste");
    send(TimelineAction::seek, {Value(5)});
    require(text(get(model.presentation(), "referenceSource")) == "file:///a.iisc",
            "reference preview used a future frame");
    const auto recipe = model.recipeShots("test prompt");
    require(array(recipe).size() == 2 && !array(get(array(recipe)[0], "conditions")).empty(),
            "recipe lost native conditions");
}
void drafts() {
    int resized = 0;
    bool allow = true;
    VideoDraft draft([&](int frames, double fps) {
        ++resized;
        return allow && frames > 0 && fps > 0;
    });
    auto edit = [&](std::string key, Value value) {
        const auto result = draft.edits().send({std::move(key), std::move(value)});
        require(result.has_value(), "draft query failed");
        return *result;
    };
    require(!edit("width", Value(33)), "non aligned width accepted");
    require(number(get(draft.values(), "width")) == 1024, "invalid width overwrote accepted draft");
    require(edit("aspectRatio", Value("9:16")), "valid aspect ratio rejected");
    require(number(get(draft.values(), "width")) == 576 && number(get(draft.values(), "height")) == 1024,
            "aspect ratio dimensions lost");
    require(edit("duration", Value(3)) && resized == 1, "duration did not use composition interface");
    const auto old = draft.values();
    allow = false;
    require(!edit("fps", Value(30)) && draft.values() == old, "failed timeline resize partially published draft");
    require(edit("camera1", Value("pan-left")) && edit("camera2", Value("pan-right")) && draft.invalid(),
            "camera conflict not guarded");
    require(edit("camera2", Value("none")), "camera edit rejected");
    draft.restoreComposition(120, 24);
    require(number(get(draft.values(), "duration")) == 5 && !has(draft.invalidInputs(), "fps"),
            "undo composition restoration lost draft values");
    InputValidity validity;
    auto record = [&](ValidityEdit request) { return validity.commands().send(request); };
    require(record({"control.a", false}).has_value(), "validity query failed");
    record({"width", false});
    const auto fields = record({{}, true, false, true});
    require(fields && has(*fields, "width") && !has(*fields, "control.a"),
            "collection refresh cleared unrelated input error");
    record({{}, true, true});
    require(!validity.invalid(), "reset did not clear draft validity");
    ComposerState composer;
    auto change = [&](std::string key, Value value) {
        const auto result = composer.edits().send({std::move(key), std::move(value)});
        require(result.has_value(), "composer query failed");
        return *result;
    };
    change("defaultPrompts", Value(Object{{"Image", Value(Object{{"prompt", Value("image default")},
                                                                 {"negativePrompt", Value("negative default")}})},
                                          {"Video", Value(Object{{"prompt", Value("video default")}})}}));
    auto home = change("promptDefaultsReady", Value(true));
    require(text(get(home, "prompt")) == "image default", "prompt defaults lost");
    change("prompt", Value("user input"));
    home = change("mediaType", Value("Video"));
    require(text(get(home, "prompt")) == "user input", "media change overwrote custom prompt");
    home = change("aspectRatio", Value("9:16"));
    require(number(get(get(home, "outputSize"), "width")) == 576, "video dimensions changed");
}
void generation() {
    GenerationPresenter presenter;
    GenerationSnapshot input;
    input.jobs = Value(Array{Value(Object{{"id", Value("old")}, {"state", Value("running")}}),
                             Value(Object{{"id", Value("new")}, {"state", Value("queued")}})});
    input.requestIds = Value(Array{Value("new")});
    input.currentResult = Value(Object{{"imageSource", Value("file:///manual.png")}});
    auto result = presenter.queries().send(input);
    require(result.has_value(), "generation projection query failed");
    require(boolean(get(*result, "generationPending")) && object(get(*result, "activeGeneration")).empty(),
            "unrelated job became submission owner");
    require(text(get(get(*result, "reviewActiveJob"), "id")) == "old", "running review job missing");
    input.requestIds = Value(Array{});
    input.results = Value(Array{});
    result = presenter.queries().send(input);
    require(array(get(*result, "reviewResults")).size() == 1, "manual result disappeared on unrelated queue update");
    input.results = Value(Array{input.currentResult});
    result = presenter.queries().send(input);
    require(array(get(*result, "reviewResults")).size() == 1, "manual result duplicated");
}
void editorTools() {
    std::map<std::string, Value, std::less<>> resources;
    for (auto name : {"tools", "graphic", "elements", "compact", "field-groups", "text", "text-definition", "photo", "view-options"}) {
        std::ifstream file(std::string(DREAMSCAPES_TEST_SOURCE_DIR) + "/resources/viewmodels/" + name + ".json");
        const std::string bytes((std::istreambuf_iterator<char>(file)), {});
        ii::json::Limits limits;
        limits.items = 500000;
        auto parsed = ii::json::parse(bytes, limits);
        if (!parsed)
            throw std::runtime_error(std::string("catalog ") + name + ": " + parsed.error().message);
        resources[name] = std::move(*parsed);
    }
    ViewCatalog catalog(std::move(resources));
    const auto sections = [&](const char *family, const Value &values) {
        auto result = catalog.queries(Domain::viewOptions).send({Read::sections, {Value(family), values}});
        require(result.has_value(), "view sections query failed");
        return *result;
    };
    for (auto family : {"snap", "grid", "guides"}) {
        const auto defaults = sections(family, Value(Object{}));
        const auto emptyFields = sections(family, Value(Object{
            {"type", Value("")}, {"units", Value("")}, {"orientation", Value("")}}));
        require(!array(defaults).empty(), "view options have no default sections");
        require(emptyFields == defaults, "empty auxiliary fields erased view option controls");
    }
    EditorToolState state(catalog);
    auto command = [&](ToolAction action, std::vector<Value> args) {
        auto result = state.commands().send({action, std::move(args)});
        require(result.has_value(), "tool command failed");
        return *result;
    };
    const auto before = state.values();
    const auto view = command(ToolAction::write, {Value("elementsViewId"), Value("stroke")});
    require(state.values() == before && view.edits.empty(), "view selection edited accepted tool fields");
    const auto group = command(ToolAction::expandLayerGroup, {Value("group-a"), Value(false)});
    require(group.edits.empty() && !group.reset && state.values() == before &&
            boolean(get(get(state.presentation(), "collapsedLayerGroups"), "group-a")),
            "collapsing a layer group created a document edit");
    command(ToolAction::expandLayerGroup, {Value("group-a"), Value(true)});
    require(!has(get(state.presentation(), "collapsedLayerGroups"), "group-a"),
            "expanded layer retained a stale collapsed state");
    command(ToolAction::write, {Value("toolId"), Value("color")});
    auto edits = command(
        ToolAction::setGraphicValues,
        {Value("developColor"),
         Value(Object{{"points", Value(Array{Value(Object{{"hue", Value(20)}})})}, {"saturation", Value(30)}})});
    require(edits.edits.size() == 3, "graphic update omitted operation or nested parameters");
    command(ToolAction::setGraphicValue, {Value("developColor"), Value("points[0].hue"), Value(40)});
    require(number(*pathValue(get(state.values(), "graphicParameters"), "points[0].hue")) == 40,
            "native graphic nested member did not apply");
    command(ToolAction::replaceGraphicCollection,
            {Value("developColor"), Value("points"), Value(Array{Value(Object{{"hue", Value(60)}})})});
    require(number(*pathValue(get(state.values(), "graphicParameters"), "points[0].hue")) == 60,
            "stale indexed edit survived collection replacement");
    command(ToolAction::write, {Value("toolId"), Value("elements")});
    command(
        ToolAction::acceptedSelection,
        {Value(Object{{"selectedLayerId", Value("shape-1")},
                      {"elementSelectionCount", Value(1)},
                      {"selectedElementSettings", Value(Object{{"selector", Value("Ellipse")},
                                                               {"field-0", Value(Array{Value(123), Value(456)})}})}}),
         Value(true)});
    require(array(get(state.values(), "field-0")).size() == 2, "selected shape dimensions lost");
    require(number(array(get(state.values(), "field-0"))[0]) == 123, "dummy dimensions replaced actual selected shape");
    command(ToolAction::acceptedSelection,
            {Value(Object{{"selectedLayerId", Value("shape-1")},
                          {"elementSelectionCount", Value(1)},
                          {"selectedElementSettings", Value(Object{{"selector", Value("Ellipse")},
                                                                   {"field-0", Value(Array{Value(123), Value(456)})},
                                                                   {"field-1", Value(256)},
                                                                   {"field-2", Value("Gradient")},
                                                                   {"field-16", Value("#123456")}})}})});
    command(ToolAction::selectElementView, {Value("fill")});
    const auto reset = command(ToolAction::resetElementView, {});
    const auto staged = get(get(state.inputSnapshot(), "settingsByTool"), "elements");
    require(!reset.edits.empty() && text(get(staged, "field-2")) == "Solid" &&
                text(get(staged, "field-16")) == "#8B7CFF",
            "batched reset lost an earlier field edit");
    require(number(get(staged, "field-1")) == 256, "fill reset changed geometry");
    GraphicDraft graphic(
        catalog, [&] { return state.inputSnapshot(); },
        [&](const ToolCommand &request) {
            require(state.commands().send(request).has_value(), "graphic tool effect failed");
        });
    auto graphics = [&](GraphicAction action, std::vector<Value> args) {
        const auto result = graphic.commands().send({action, std::move(args)});
        require(result.has_value(), "graphic query failed");
        return *result;
    };
    const auto panels = get(catalog.resource("graphic"), "panels");
    for (const auto &panel : array(panels)) {
        require(graphics(GraphicAction::selectView, {get(panel, "id")}), "native graphic view unavailable");
        require(get(graphic.selectedView(), "id") == get(panel, "id"), "native graphic navigation changed target");
        require(graphic.parameters(text(get(panel, "operation"))).kind() == ii::core::ValueKind::object,
                "native graphic parameters unavailable");
    }
    auto activation = graphic.activations().send({Value(Object{{"value", Value("apply")}}), Value(Object{})});
    require(activation && activation->route == GraphicRoute::execute && activation->action == "apply",
            "graphic apply route changed");
    activation =
        graphic.activations().send({Value(Object{{"value", Value("Daylight")}, {"api", Value("whiteBalancePreset")}}),
                                    Value(Object{{"uiOnly", Value(true)}})});
    require(activation && activation->action == "helper" && text(get(activation->parameters, "preset")) == "Daylight",
            "white balance helper lost preset");
    activation = graphic.activations().send(
        {Value(Object{}), Value(Object{{"editor", Value("hostImagePicker")}, {"parameter", Value("image")}})});
    require(activation && activation->route == GraphicRoute::source, "image picker route did not request source");
    command(ToolAction::write, {Value("toolId"), Value("text")});
    command(ToolAction::editText, {Value("textHorizontalConstraint"), Value("Right")});
    require(boolean(get(state.textValues(), "textLayoutEnabled")), "text layout not enabled by constraint edit");
    command(ToolAction::textLinePreset, {Value("150%")});
    require(number(get(state.textValues(), "textLineHeight")) == number(get(state.textValues(), "field-2")) * 1.5,
            "text line preset did not use accepted font size");
}
void newDraftAndCommands() {
    const Value preset(Object{{"id", Value("test")},
                              {"name", Value("Landscape")},
                              {"width", Value(1800)},
                              {"height", Value(1200)},
                              {"unit", Value("px")},
                              {"ppi", Value(300)}});
    CanvasDraft canvas(
        [&](int, std::string_view query) {
            return query == "missing" ? Value(Array{}) : Value(Array{Value(Object{{"presets", Value(Array{preset})}})});
        },
        [](double w, double h, std::string_view, double ppi, std::string_view) {
            return Value(Object{{"valid", Value(std::isfinite(w) && std::isfinite(h) && w > 0 && h > 0 && ppi > 0)},
                                {"pixelWidth", Value(std::isfinite(w) ? w : 0)},
                                {"pixelHeight", Value(std::isfinite(h) ? h : 0)}});
        },
        [](double value, std::string_view from, std::string_view to, double ppi) {
            return from == to ? value : to == "in" ? value / ppi : value * ppi;
        });
    const auto begin = canvas.commands().send({CanvasDraftAction::begin});
    require(begin && text(get(begin->presentation, "selectionName")) == "Landscape",
            "canvas begin lost default preset");
    require(text(get(begin->presentation, "aspectRatio")) == "3:2", "canvas aspect ratio wrong");
    auto created = canvas.commands().send({CanvasDraftAction::create});
    require(created && created->created && text(get(*created->created, "presetId")) == "test",
            "native create lost preset identity");
    auto changed = canvas.commands().send({CanvasDraftAction::changeUnit, {}, Value("in")});
    require(changed && text(get(changed->presentation, "draftWidth")) == "6", "unit conversion failed");
    (void)canvas.commands().send({CanvasDraftAction::write, "draftWidth", Value("bad")});
    created = canvas.commands().send({CanvasDraftAction::create});
    require(created && !created->created, "invalid canvas was created");
    changed = canvas.commands().send({CanvasDraftAction::search, {}, Value("missing")});
    require(changed && number(get(changed->presentation, "resultCount")) == 0, "empty search retained stale preset");

    ImageSelection selection;
    Value a(Object{{"imageSource", Value("file:///a.png")}, {"mediaType", Value("Image")}}),
        b(Object{{"imageSource", Value("file:///b.png")}, {"mediaType", Value("Image")}}),
        c(Object{{"imageSource", Value("file:///c.png")}, {"mediaType", Value("Image")}});
    (void)selection.commands().send({ImageSelectionAction::setEntries, Value(Array{a, b, c})});
    ImageSelectionCommand command{ImageSelectionAction::selectAt};
    command.index = 1;
    command.previous = 0;
    command.additive = true;
    auto selected = selection.commands().send(command);
    require(selected && array(get(*selected, "selectedImages")).size() == 2,
            "additive image selection lost previous focus");
    selected = selection.commands().send({ImageSelectionAction::setEntries, Value(Array{c, b, a})});
    require(selected && array(get(*selected, "selectedImages")).size() == 2, "reorder discarded selected identities");
    const Value ownership(Object{{"key", Value("files/a.iisc")}, {"containerId", Value("drive")}});
    require(ownsDownload(true, ownership, "files/a.iisc", "drive") &&
                !ownsDownload(true, ownership, "files/b.iisc", "drive") &&
                !ownsDownload(false, ownership, "files/a.iisc", "drive"),
            "download cancellation crossed ownership boundary");
    EditorCommandRouter router;
    EditorInvocation invocation{"file", "field-12",
                                Value(Object{{"field-1", Value("IISC")}, {"field-11", Value(true)}})};
    auto route = router.routes().send(invocation);
    require(route && route->route == EditorRoute::saveDocumentAs, "IISC save routing changed");
    invocation = {"background", "field-15",
                  Value(Object{{"field-13", Value("Generate")}, {"replacePrompt", Value("new background")}})};
    route = router.routes().send(invocation);
    auto recipe = router.generation().send(invocation);
    require(route && route->route == EditorRoute::generate && recipe &&
                text(get(recipe->values, "field-0")) == "new background",
            "background generation recipe lost prompt");
    ReviewCommands review;
    ReviewKeyInput input;
    input.key = ReviewKey::down;
    input.count = 24;
    input.focusedIndex = 23;
    input.columns = 4;
    auto key = review.keys().send(input);
    require(key && key->action == ReviewAction::focus && key->index == 23, "review movement left grid bounds");
    input.key = ReviewKey::shortlist;
    input.command = true;
    key = review.keys().send(input);
    require(key && key->action == ReviewAction::ignored, "modified text shortcut changed review");
    input.command = false;
    input.key = ReviewKey::escape;
    input.overlay = true;
    key = review.keys().send(input);
    require(key && key->action == ReviewAction::back, "overlay escape lost priority");
    PhotoPresentation photo;
    PhotoSnapshot snapshot;
    snapshot.settings = Value(Object{{"captureScope", Value("Window")}, {"captureTarget", Value("missing")},
                                     {"cameraAngle",Value("270°")}});
    snapshot.targets = Value(Array{Value(Object{{"id", Value("screen")}, {"kind", Value("Screen")}}),
                                   Value(Object{{"id", Value("window")}, {"kind", Value("Window")}})});
    snapshot.recordingMilliseconds = 3661000;
    auto presentation = photo.queries().send(snapshot);
    require(presentation && text(get(get(*presentation, "captureTarget"), "id")) == "window" &&
                text(get(*presentation, "recordingTime")) == "01:01:01" &&
                number(get(*presentation,"orientation")) == 270,
            "photo target/time projection failed");
    const Value poster(Object{{"name", Value("01234567-1234-1234-1234-012345678901-poster.png")},
                              {"previewSource", Value("file:///v-poster.png?revision=1")}});
    const auto video = historyEntry(poster);
    require(text(get(video, "mediaSource")) == "file:///v-0001.mp4" &&
                text(get(video, "name")) == "01234567-1234-1234-1234-012345678901-0001.mp4",
            "history video projection corrupted source");
}
void projectionRowsAndWorkers() {
    RowProjector rows;
    const auto row = [](const char *id, int n) { return Value(Object{{"id", Value(id)}, {"n", Value(n)}}); };
    auto result =
        rows.queries().send({Value(Array{row("a", 1), row("b", 2)}), Value(Array{row("b", 3), row("a", 1)}), "id"});
    require(result && result->valid && result->changes.size() == 2 &&
                result->changes.front().kind == RowChangeKind::move,
            "keyed projection rebuilt stable rows");
    result = rows.queries().send({Value(Array{row("a", 1)}), Value(Array{row("a", 1), row("a", 2)}), "id"});
    require(result && !result->valid && result->changes.empty(), "duplicate row identifiers partially applied");
    StorageSnapshot input;
    Array aliases;
    for (int i = 0; i < 1000; ++i)
        aliases.emplace_back(Object{{"section", Value("files")},
                                    {"exists", Value(true)},
                                    {"relativePath", Value("folder/file" + std::to_string(i) + ".png")},
                                    {"name", Value("file" + std::to_string(i) + ".png")},
                                    {"key", Value("item-" + std::to_string(i))}});
    input.aliases = Value(aliases);
    input.directory = "folder";
    input.query = "file99";
    StorageProjector storage;
    std::promise<ii::runtime::QueryReply<StoragePresentation>> promise;
    auto future = promise.get_future();
    const auto started = std::chrono::steady_clock::now();
    const auto admission = storage.queries().post(viewRuntime(), input, [&](auto reply) {
        require(ii::runtime::Runtime::is_worker_thread(), "file projection ran on caller thread");
        promise.set_value(std::move(reply));
    });
    require(bool(admission), "native worker request rejected");
    require(future.wait_for(std::chrono::seconds(10)) == std::future_status::ready, "native worker delivery timed out");
    auto completed = future.get();
    require(completed && array(completed->entries).size() == 11, "native file projection lost matching entries");
    require(array(input.aliases).size() == 1000, "native projection changed source metadata");
    std::cout << "1000-entry native projection ms="
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() << '\n';
}
void presentationQueries() {
    ViewProjection projection;
    const auto query = [&](ProjectionRead operation, std::vector<Value> arguments) {
        auto result = projection.queries().send({operation, std::move(arguments)});
        require(result.has_value(), "native presentation query rejected");
        return *result;
    };
    const Value rows(Array{Value(Object{{"id", Value("one")}, {"name", Value("First")}}),
                           Value(Object{{"id", Value("two")}, {"name", Value("")}})});
    require(array(query(ProjectionRead::labels, {rows, Value("name"), Value("id")})) ==
                Array{Value("First"), Value("two")}, "model label fallback was lost");
    require(number(query(ProjectionRead::index, {rows, Value("two"), Value("id")})) == 1,
            "selection lookup lost model identity");
    require(number(query(ProjectionRead::index, {Value(Array{Value(1)}), Value(1.0), Value()})) == 0,
            "integer and real representations of a UI number differ");
    require(number(query(ProjectionRead::index, {Value(Array{Value(1)}), Value(true), Value()})) == -1,
            "boolean was coerced to a numeric choice");
    require(array(query(ProjectionRead::indices, {Value(5), Value(-3), Value(-1)})) == Array{Value(2),Value(3)},
            "negative range boundaries differ from the previous view");
    require(array(query(ProjectionRead::indices, {Value(5), Value(4), Value(1)})).empty(),
            "inverted range returned data");
    require(array(query(ProjectionRead::indices, {Value(5), Value(-100), Value(100)})).size() == 5,
            "range did not clamp to its snapshot");
    const auto choices = query(ProjectionRead::counts, {Value(11)});
    require(array(choices).size() == 18 && number(array(choices)[8]) == 11,
            "custom output count was lost or unsorted");
    const auto next = query(ProjectionRead::nextChoice, {Value(Array{Value(1),Value(2)}), Value(1.0)});
    require(number(get(next, "value")) == 2, "next choice changed numeric selection semantics");
    const Value tracks(Array{Value(Object{{"id",Value("a")},{"type",Value("Shots")}}),
                             Value(Object{{"id",Value("b")},{"type",Value("Contents")}}),
                             Value(Object{{"id",Value("c")},{"type",Value("Audio")}})});
    const auto layout = query(ProjectionRead::timelineLayout, {tracks, Value("Curves")});
    require(number(get(layout,"height")) == 256 && number(get(layout,"curveHeight")) == 80 &&
            number(get(array(get(layout,"tracks"))[2],"top")) == 208,
            "native timeline lanes overlap or lost curve extent");
    require(!has(array(tracks)[0], "top"), "layout changed source tracks");
    require(text(query(ProjectionRead::timecode, {Value(24), Value(24)})) == "00:00:01:00" &&
            text(query(ProjectionRead::timecode, {Value(86401), Value(24)})) == "01:00:00:01" &&
            text(query(ProjectionRead::timecode, {Value(24), Value(0)})) == "--:--:--:--",
            "timecode carried frames incorrectly or accepted an invalid FPS");
    const auto sizes = query(ProjectionRead::sizeChoices, {Value(Array{Value(576)}), Value("9:16")});
    require(text(array(sizes)[0]) == "576 × 1024", "resolution choices differ from generation request sizes");
    auto rejected = projection.queries().send({ProjectionRead::indices, {Value(2147483647), Value(0)}});
    require(!rejected, "unbounded range allocation was admitted");
}
void nativePlaybackAndPalette() {
    NumberInputs numbers;
    for (const auto &value : {std::string("F024"),std::string("24f"),std::string("Source F024"),std::string("24")}) {
        const auto parsed = numbers.queries().send({NumberInputKind::frame,value});
        require(parsed && parsed->accepted && parsed->value == 24,"native frame text lost a supported input format");
    }
    for (const auto &value : {std::string(""),std::string("12junk"),std::string("1.5"),std::string("2147483648")}) {
        const auto parsed = numbers.queries().send({NumberInputKind::frame,value});
        require(parsed && !parsed->accepted,"partial, fractional or overflowing frame input accepted");
    }
    auto percentage = numbers.queries().send({NumberInputKind::percentage,"42%"});
    require(percentage && percentage->accepted && percentage->value == 42,"native percentage was not parsed");
    percentage = numbers.queries().send({NumberInputKind::percentage,"101%"});
    require(percentage && !percentage->accepted,"out-of-range percentage was accepted");
    TimelinePlayback playback;
    const auto start = std::chrono::steady_clock::time_point{};
    const auto send = [&](PlaybackAction action, int milliseconds, double frame = 0, bool loop = false, int step = 1) {
        auto result = playback.commands().send({action, start + std::chrono::milliseconds(milliseconds),
                                               frame, 24, 120, step, loop});
        require(result.has_value(), "native playback command rejected");
        return *result;
    };
    require(send(PlaybackAction::start, 0).playing, "native playback did not start");
    require(send(PlaybackAction::tick, 1000).frame == 24, "playback followed wakeup count instead of elapsed time");
    const auto delayed = send(PlaybackAction::tick, 5100, 24, true);
    require(delayed.playing && delayed.frame == 2, "loop playback lost its elapsed-time anchor");
    const auto ended = send(PlaybackAction::tick, 6000);
    require(!ended.playing && ended.frame == 0, "non-loop playback did not stop at its end");
    require(send(PlaybackAction::seek, 6000, 17, false, 5).frame == 15, "native seek ignored snap interval");
    require(send(PlaybackAction::seek, 6000, 500).frame == 119, "native seek escaped timeline bounds");
    const auto invalid = send(PlaybackAction::seek, 6000, std::numeric_limits<double>::quiet_NaN());
    require(!invalid.accepted && invalid.frame == 119, "invalid seek replaced the last accepted frame");
    send(PlaybackAction::start, 7000, 40);
    send(PlaybackAction::stop, 7010);
    require(!send(PlaybackAction::tick, 9000).playing, "retired playback resumed on a late wakeup");
    BackgroundPalette palette;
    const Value preset(Object{{"name", Value("Single")}, {"colors", Value(Array{Value("#123456")})}});
    const Value saved(Array{Value(Object{{"name",Value("Saved")}, {"stops",Value(Array{})}})});
    auto presets = palette.queries().send({PaletteAction::presets, Value(Array{preset}), saved, {}});
    require(presets && array(*presets).size() == 2 &&
            number(get(array(get(array(*presets)[0],"stops"))[0],"position"), -1) == 0,
            "single-color gradient had a nonfinite stop or saved preset was removed");
    require(!has(preset, "stops") && array(saved).size() == 1, "palette projection changed original settings");
    auto appended = palette.queries().send({PaletteAction::save, {}, saved, preset});
    require(appended && array(*appended).size() == 2, "saving a gradient discarded prior presets");
}
} // namespace
int main() {
    try {
        values();
        timeline();
        drafts();
        generation();
        editorTools();
        newDraftAndCommands();
        projectionRowsAndWorkers();
        presentationQueries();
        nativePlaybackAndPalette();
        std::cout << "native view value, timeline, draft, generation, editor and worker contracts passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
