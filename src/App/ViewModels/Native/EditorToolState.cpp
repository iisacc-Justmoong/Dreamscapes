#include "EditorToolState.h"

namespace dreamscapes::vm {
EditorToolState::EditorToolState(ViewCatalog &catalog) : catalog_(catalog), commands_([this](const auto &command) { return execute(command); }) {
    data_ = {{"toolId", Value("elements")}, {"settingsByTool", Value(Object{})}, {"graphicViewsByTool", Value(Object{})}, {"compactViewsByTool", Value(Object{})},
        {"fileViewId", Value("information")}, {"elementsViewId", Value("rectangle")}, {"elementsMultipleSelection", Value(true)}, {"elementsSelectionScope", Value("All shapes")},
        {"inspectedElementId", Value("")}, {"inspectedElementShape", Value("")}, {"notice", Value("")}, {"generationModels", Value(Array{})}, {"hasEngine", Value(false)}};
}
Value EditorToolState::read(Domain domain, Read operation, std::vector<Value> args) const {
    const auto result = catalog_.queries(domain).send({operation, std::move(args)}); return result ? *result : Value();
}
Value EditorToolState::definition() const {
    auto result = read(Domain::tools, Read::tool, {data_.at("toolId")});
    if (boolean(data_.at("hasEngine"))) result = read(Domain::tools, Read::runtimeDefinition, {result});
    if (text(data_.at("toolId")) == "generative") {
        auto fields = array(get(result, "fields")); Array options{Value("Auto")}, labels{Value("Current model")};
        for (const auto &model : array(data_.at("generationModels"))) { options.push_back(get(model, "id")); labels.push_back(text(get(model, "name")).empty() ? get(model, "id") : get(model, "name")); }
        for (auto &field : fields) if (text(get(field, "id")) == "field-2") field = merged(field, Value(Object{{"initial", Value("Auto")}, {"options", Value(options)}, {"optionLabels", Value(labels)}}));
        result = merged(result, Value(Object{{"fields", Value(fields)}}));
    }
    return result;
}
Value EditorToolState::values() const {
    const auto tool = text(data_.at("toolId"));
    const auto &stored = get(data_.at("settingsByTool"), tool);
    auto result = stored.kind() == ii::core::ValueKind::null ? read(Domain::tools, Read::defaults, {definition()}) : stored;
    if (!boolean(data_.at("hasEngine"))) return result;
    if (tool == "elements" && number(get(accepted_, "elementSelectionCount")) > 0) return merged(result, get(accepted_, "selectedElementSettings"));
    if (tool == "layers") {
        auto actual = object(result); actual["field-1"] = text(get(accepted_, "generationSeed")).empty() ? Value("Unknown") : get(accepted_, "generationSeed");
        const auto &layer = get(accepted_, "selectedLayer");
        if (!text(get(layer, "id")).empty()) { actual["field-8"] = get(layer, "lock"); actual["field-9"] = get(layer, "preserveAlpha"); actual["field-10"] = Value(number(get(layer, "opacity")) * 100); actual["field-20"] = get(layer, "blend"); actual["field-19"] = get(layer, "name"); }
        return Value(actual);
    }
    if (tool == "canvas") return merged(result, Value(Object{{"field-0", Value(Array{get(accepted_, "editingWidth"), get(accepted_, "editingHeight")})}}));
    return result;
}
Value EditorToolState::presentation() const { auto result = data_; result["definition"] = definition(); result["values"] = values(); return Value(result); }
ToolReply EditorToolState::publishGraphic(std::string_view operation, const Value &draft) {
    const auto tool = text(data_.at("toolId")); auto next = object(values()); auto drafts = object(get(Value(next), "graphicDrafts")); drafts[std::string(operation)] = draft;
    next["graphicDrafts"] = Value(drafts); next["graphicOperation"] = Value(operation); next["graphicParameters"] = nested(read(Domain::graphics, Read::operationDefaults, {Value(operation)}), draft);
    auto all = object(data_.at("settingsByTool")); all[tool] = Value(next); data_["settingsByTool"] = Value(all);
    return {{{tool, "graphicDrafts", next["graphicDrafts"]}, {tool, "graphicOperation", next["graphicOperation"]}, {tool, "graphicParameters", next["graphicParameters"]}}};
}
ToolReply EditorToolState::execute(const ToolCommand &command) {
    static const Value empty; const auto arg = [&](std::size_t i) -> const Value & { return i < command.arguments.size() ? command.arguments[i] : empty; };
    const auto key = text(arg(0));
    switch (command.action) {
    case ToolAction::write: if (data_.contains(key)) data_[key] = arg(1); return {};
    case ToolAction::setValue: {
        auto next = object(values()); if (next.contains(key) && next[key] == arg(1)) return {};
        next[key] = arg(1); const auto tool = text(data_.at("toolId")); auto all = object(data_.at("settingsByTool")); all[tool] = Value(next); data_["settingsByTool"] = Value(all); return {{{tool, key, arg(1)}}};
    }
    case ToolAction::setGraphicValue: {
        const auto draft = get(get(values(), "graphicDrafts"), key);
        return publishGraphic(key, merged(draft, Value(Object{{text(arg(1)), arg(2)}})));
    }
    case ToolAction::setGraphicValues: return publishGraphic(key, merged(get(get(values(), "graphicDrafts"), key), arg(1)));
    case ToolAction::replaceGraphicCollection: {
        Object draft; const auto path = text(arg(1)); const auto current = get(get(values(), "graphicDrafts"), key);
        for (const auto &[name, value] : object(current)) if (name != path && !name.starts_with(path + '[')) draft[name] = value;
        draft[path] = arg(2); return publishGraphic(key, Value(draft));
    }
    case ToolAction::resetGraphicView: {
        Object draft; const auto current = get(get(values(), "graphicDrafts"), key);
        for (const auto &[name, value] : object(current)) {
            bool retained = true; for (const auto &[parent, initial] : object(arg(1))) if (initial.kind() == ii::core::ValueKind::array && name.starts_with(parent + '[')) retained = false;
            if (retained) draft[name] = value;
        }
        return publishGraphic(key, merged(Value(draft), arg(1)));
    }
    case ToolAction::resetTool: {
        const auto tool = text(data_.at("toolId")); auto all = object(data_.at("settingsByTool")); all[tool] = read(Domain::tools, Read::defaults, {definition()}); data_["settingsByTool"] = Value(all); data_["notice"] = Value(""); return {{}, true};
    }
    case ToolAction::acceptedSelection: {
        accepted_ = arg(0); const auto &selected = get(accepted_, "selectedElementSettings");
        if (object(selected).empty()) { data_["inspectedElementId"] = Value(""); data_["inspectedElementShape"] = Value(""); return {}; }
        auto next = merged(read(Domain::tools, Read::defaults, {read(Domain::tools, Read::tool, {Value("elements")})}), selected);
        auto all = object(data_.at("settingsByTool")); all["elements"] = next; data_["settingsByTool"] = Value(all);
        const auto selectedId = get(accepted_, "selectedLayerId"), shape = get(selected, "selector"); const auto view = text(data_.at("elementsViewId"));
        if ((boolean(arg(1)) || selectedId != data_.at("inspectedElementId") || shape != data_.at("inspectedElementShape")) && (view == "rectangle" || view == "ellipse" || view == "polygon" || view == "line")) {
            const auto name = text(shape); data_["elementsViewId"] = Value(name == "Rectangle" ? "rectangle" : name == "Ellipse" ? "ellipse" : name == "Polygon" ? "polygon" : name == "Line / Arrow" ? "line" : "inspect");
        }
        data_["inspectedElementId"] = selectedId; data_["inspectedElementShape"] = shape; return {};
    }
    }
    return {};
}
} // namespace dreamscapes::vm
