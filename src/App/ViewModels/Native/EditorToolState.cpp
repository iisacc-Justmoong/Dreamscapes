#include "EditorToolState.h"

namespace dreamscapes::vm {
EditorToolState::EditorToolState(ViewCatalog &catalog)
    : catalog_(catalog), commands_([this](const auto &command) { return execute(command); }) {
    data_ = {{"toolId", Value("elements")},
             {"settingsByTool", Value(Object{})},
             {"graphicViewsByTool", Value(Object{})},
             {"compactViewsByTool", Value(Object{})},
             {"fileViewId", Value("information")},
             {"elementsViewId", Value("rectangle")},
             {"elementsMultipleSelection", Value(true)},
             {"elementsSelectionScope", Value("All shapes")},
             {"inspectedElementId", Value("")},
             {"inspectedElementShape", Value("")},
             {"notice", Value("")},
             {"generationModels", Value(Array{})},
             {"collapsedLayerGroups", Value(Object{})},
             {"hasEngine", Value(false)},
             {"layerAdjustmentValues", Value(Object{})},
             {"layerAdjustmentKey", Value("")},
             {"lastLayerAdjustmentTool", Value("color")}};
}
Value EditorToolState::read(Domain domain, Read operation, std::vector<Value> args) const {
    const auto result = catalog_.queries(domain).send({operation, std::move(args)});
    return result ? *result : Value();
}
Value EditorToolState::definition() const {
    auto result = read(Domain::tools, Read::tool, {data_.at("toolId")});
    if (boolean(data_.at("hasEngine")))
        result = read(Domain::tools, Read::runtimeDefinition, {result});
    if (text(data_.at("toolId")) == "generative") {
        auto fields = array(get(result, "fields"));
        Array options{Value("Auto")}, labels{Value("Current model")};
        for (const auto &model : array(data_.at("generationModels"))) {
            options.push_back(get(model, "id"));
            labels.push_back(text(get(model, "name")).empty() ? get(model, "id") : get(model, "name"));
        }
        for (auto &field : fields)
            if (text(get(field, "id")) == "field-2")
                field = merged(field, Value(Object{{"initial", Value("Auto")},
                                                   {"options", Value(options)},
                                                   {"optionLabels", Value(labels)}}));
        result = merged(result, Value(Object{{"fields", Value(fields)}}));
    }
    return result;
}
Value EditorToolState::values() const {
    const auto tool = text(data_.at("toolId"));
    const auto &stored = get(data_.at("settingsByTool"), tool);
    auto result =
        stored.kind() == ii::core::ValueKind::null ? read(Domain::tools, Read::defaults, {definition()}) : stored;
    if (!boolean(data_.at("hasEngine")))
        return result;
    if (tool == "elements" && number(get(accepted_, "elementSelectionCount")) > 0)
        return merged(result, get(accepted_, "selectedElementSettings"));
    if (tool == "layers") {
        auto actual = object(result);
        actual["field-1"] =
            text(get(accepted_, "generationSeed")).empty() ? Value("Unknown") : get(accepted_, "generationSeed");
        const auto &layer = get(accepted_, "selectedLayer");
        if (!text(get(layer, "id")).empty()) {
            actual["field-8"] = get(layer, "lock");
            actual["field-9"] = get(layer, "preserveAlpha");
            actual["field-10"] = Value(number(get(layer, "opacity")) * 100);
            actual["field-20"] = get(layer, "blend");
            actual["field-19"] = get(layer, "name");
        }
        return Value(actual);
    }
    if (tool == "canvas")
        return merged(
            result,
            Value(Object{{"field-0", Value(Array{get(accepted_, "editingWidth"), get(accepted_, "editingHeight")})}}));
    return result;
}
Value EditorToolState::elementValues() const {
    const auto stored = text(data_.at("toolId")) == "elements" ? values() : get(data_.at("settingsByTool"), "elements");
    auto result = stored.kind() == ii::core::ValueKind::null
                      ? read(Domain::tools, Read::defaults, {read(Domain::tools, Read::tool, {Value("elements")})})
                      : stored;
    if (number(get(accepted_, "elementSelectionCount")) > 0)
        result = merged(result, get(accepted_, "selectedElementSettings"));
    return result;
}
Value EditorToolState::textValues() const {
    return merged(read(Domain::text, Read::defaultsFor, {get(catalog_.resource("text-definition"), "fields")}),
                  text(data_.at("toolId")) == "text" ? values() : get(data_.at("settingsByTool"), "text"));
}
bool EditorToolState::elementFieldEnabled(std::string_view field) const {
    for (const auto &candidate : array(get(accepted_, "elementCandidates")))
        if (boolean(get(candidate, "active")))
            return boolean(get(candidate, "editable")) ||
                   (boolean(get(candidate, "resizable")) &&
                    (field == "field-0" || field == "field-14" || field == "field-15"));
    return true;
}
std::string EditorToolState::formattedElement(std::string_view field, int index) const {
    const auto all = elementValues();
    const auto &value = get(all, field);
    if (field == "field-0") {
        if (index < 0 || static_cast<std::size_t>(index) >= array(value).size())
            return {};
        return numberText(Value(Object{{"decimals", Value(2)}, {"unit", Value("px")}}), array(value)[index], true);
    }
    const auto spec = read(Domain::elements, Read::field, {Value(field)});
    return numberText(spec, value, false);
}
bool EditorToolState::textOptionAvailable(std::string_view field, std::string_view option) const {
    return !(field == "textBlend" && (option == "Soft light" || option == "Difference")) &&
           !(field == "textNoisePattern" && option == "Blue noise") &&
           !((field == "textHorizontalConstraint" || field == "textVerticalConstraint") && option == "Scale");
}
Value EditorToolState::inputSnapshot() const {
    auto result = data_;
    result["values"] = values();
    return Value(std::move(result));
}
Value EditorToolState::presentation() const {
    auto result = data_;
    const auto definitionValue = definition(), current = values();
    result["definition"] = definitionValue;
    result["values"] = current;
    result["elementValues"] = elementValues();
    result["textValues"] = textValues();
    result["photoValues"] =
        merged(read(Domain::photo, Read::defaults, {}),
               text(data_.at("toolId")) == "camera-photo" ? current : get(data_.at("settingsByTool"), "camera-photo"));
    Value active;
    Array scoped;
    const auto scope = text(data_.at("elementsSelectionScope"));
    for (const auto &candidate : array(get(accepted_, "elementCandidates"))) {
        if (boolean(get(candidate, "active")) && active.kind() == ii::core::ValueKind::null)
            active = candidate;
        if (scope != "Selected shapes" || boolean(get(candidate, "selected")))
            scoped.push_back(candidate);
    }
    result["activeElementCandidate"] = active;
    result["scopedElementCandidates"] = Value(scoped);
    result["elementFields"] = read(Domain::elements, Read::controls,
                                   {data_.at("elementsViewId"), Value(text(get(active, "shape"), "Rectangle"))});
    const auto tool = text(data_.at("toolId"));
    const auto views = read(Domain::compact, Read::views, {Value(tool)});
    const auto saved = read(Domain::compact, Read::view, {Value(tool), get(data_.at("compactViewsByTool"), tool)});
    Value selectedView;
    if (saved.kind() != ii::core::ValueKind::null &&
        (text(get(saved, "mode")).empty() || get(saved, "mode") == get(current, "selector")))
        selectedView = saved;
    if (selectedView.kind() == ii::core::ValueKind::null)
        for (const auto &view : array(views))
            if (get(view, "mode") == get(current, "selector")) {
                selectedView = view;
                break;
            }
    if (selectedView.kind() == ii::core::ValueKind::null && !array(views).empty())
        selectedView = array(views).front();
    result["compactViewCatalog"] = views;
    result["compactSelectedView"] = selectedView;
    result["compactCurrentViewId"] = Value(text(get(selectedView, "id")));
    Array fields;
    for (const auto &row : array(get(selectedView, "fields"))) {
        Value field;
        for (const auto &entry : array(get(definitionValue, "fields")))
            if (get(entry, "id") == get(row, "id")) {
                field = entry;
                break;
            }
        if (tool == "generative" && text(get(row, "id")) == "field-2")
            fields.push_back(merged(field, Value(Object{{"icon", get(row, "icon")}, {"height", get(row, "height")}})));
        else
            fields.push_back(merged(field, row));
    }
    result["compactPresentedFields"] = Value(fields);
    return Value(result);
}
ToolReply EditorToolState::publishGraphic(std::string_view operation, const Value &draft) {
    const auto tool = text(data_.at("toolId"));
    auto next = object(values());
    auto drafts = object(get(Value(next), "graphicDrafts"));
    drafts[std::string(operation)] = draft;
    next["graphicDrafts"] = Value(drafts);
    next["graphicOperation"] = Value(operation);
    next["graphicParameters"] = nested(read(Domain::graphics, Read::operationDefaults, {Value(operation)}), draft);
    auto all = object(data_.at("settingsByTool"));
    all[tool] = Value(next);
    data_["settingsByTool"] = Value(all);
    return {{{tool, "graphicDrafts", next["graphicDrafts"]},
             {tool, "graphicOperation", next["graphicOperation"]},
             {tool, "graphicParameters", next["graphicParameters"]}}};
}
ToolReply EditorToolState::execute(const ToolCommand &command) {
    static const Value empty;
    const auto arg = [&](std::size_t i) -> const Value & {
        return i < command.arguments.size() ? command.arguments[i] : empty;
    };
    const auto key = text(arg(0));
    switch (command.action) {
    case ToolAction::prepareLayerAdjustment: {
        data_["layerAdjustmentKey"] = get(arg(0), "key");
        auto initial = object(read(Domain::tools, Read::defaults, {read(Domain::tools, Read::tool, {Value("color")})}));
        for (const auto *field :
             {"field-1", "field-2", "field-4", "field-8", "field-12", "field-16", "field-17", "field-20", "field-21",
              "field-25", "field-28", "field-32", "field-36", "field-42", "field-43", "curveAmount"})
            initial[field] = Value(0);
        data_["layerAdjustmentValues"] = merged(Value(initial), arg(1));
        return {};
    }
    case ToolAction::editLayerAdjustment: {
        auto values = object(data_.at("layerAdjustmentValues"));
        values[key] = arg(1);
        data_["layerAdjustmentValues"] = Value(values);
        data_["lastLayerAdjustmentTool"] = Value("color");
        auto settings = object(data_.at("settingsByTool"));
        settings["color"] = Value(std::move(values));
        data_["settingsByTool"] = Value(std::move(settings));
        return {};
    }
    case ToolAction::restoreSettings: {
        auto restored = object(arg(0));
        const auto &existing = data_.at("settingsByTool");
        for (const auto *tool : {"color", "auto-enhance", "masking", "effects"}) {
            const auto &draft = get(existing, tool);
            if (!has(draft, "graphicDrafts"))
                continue;
            auto preserved = object(get(Value(restored), tool));
            for (const auto *field : {"graphicDrafts", "graphicOperation", "graphicParameters"})
                if (has(draft, field))
                    preserved[field] = get(draft, field);
            restored[tool] = Value(std::move(preserved));
        }
        if (has(existing, "camera-photo"))
            restored["camera-photo"] = get(existing, "camera-photo");
        data_["settingsByTool"] = Value(std::move(restored));
        data_["notice"] = Value("");
        return {};
    }
    case ToolAction::write:
        if (data_.contains(key))
            data_[key] = arg(1);
        return {};
    case ToolAction::expandLayerGroup: {
        if (key.empty()) return {};
        auto groups = object(data_.at("collapsedLayerGroups"));
        if (boolean(arg(1))) groups.erase(key);
        else groups[key] = Value(true);
        data_["collapsedLayerGroups"] = Value(std::move(groups));
        return {}; // View-only state: no document edit or reset command.
    }
    case ToolAction::setValue: {
        auto next = object(values());
        if (next.contains(key) && next[key] == arg(1))
            return {};
        next[key] = arg(1);
        const auto tool = text(data_.at("toolId"));
        auto all = object(data_.at("settingsByTool"));
        all[tool] = Value(next);
        data_["settingsByTool"] = Value(all);
        return {{{tool, key, arg(1)}}};
    }
    case ToolAction::setGraphicValue: {
        const auto draft = get(get(values(), "graphicDrafts"), key);
        return publishGraphic(key, merged(draft, Value(Object{{text(arg(1)), arg(2)}})));
    }
    case ToolAction::setGraphicValues:
        return publishGraphic(key, merged(get(get(values(), "graphicDrafts"), key), arg(1)));
    case ToolAction::replaceGraphicCollection: {
        Object draft;
        const auto path = text(arg(1));
        const auto current = get(get(values(), "graphicDrafts"), key);
        for (const auto &[name, value] : object(current))
            if (name != path && !name.starts_with(path + '['))
                draft[name] = value;
        draft[path] = arg(2);
        return publishGraphic(key, Value(draft));
    }
    case ToolAction::resetGraphicView: {
        Object draft;
        const auto current = get(get(values(), "graphicDrafts"), key);
        for (const auto &[name, value] : object(current)) {
            bool retained = true;
            for (const auto &[parent, initial] : object(arg(1)))
                if (initial.kind() == ii::core::ValueKind::array && name.starts_with(parent + '['))
                    retained = false;
            if (retained)
                draft[name] = value;
        }
        return publishGraphic(key, merged(Value(draft), arg(1)));
    }
    case ToolAction::resetTool: {
        const auto tool = text(data_.at("toolId"));
        auto all = object(data_.at("settingsByTool"));
        all[tool] = read(Domain::tools, Read::defaults, {definition()});
        data_["settingsByTool"] = Value(all);
        data_["notice"] = Value("");
        return {{}, true};
    }
    case ToolAction::acceptedSelection: {
        accepted_ = arg(0);
        const auto &selected = get(accepted_, "selectedElementSettings");
        if (object(selected).empty()) {
            data_["inspectedElementId"] = Value("");
            data_["inspectedElementShape"] = Value("");
            return {};
        }
        auto next = merged(read(Domain::tools, Read::defaults, {read(Domain::tools, Read::tool, {Value("elements")})}),
                           selected);
        auto all = object(data_.at("settingsByTool"));
        all["elements"] = next;
        data_["settingsByTool"] = Value(all);
        const auto selectedId = get(accepted_, "selectedLayerId"), shape = get(selected, "selector");
        const auto view = text(data_.at("elementsViewId"));
        if ((boolean(arg(1)) || selectedId != data_.at("inspectedElementId") ||
             shape != data_.at("inspectedElementShape")) &&
            (view == "rectangle" || view == "ellipse" || view == "polygon" || view == "line")) {
            const auto name = text(shape);
            data_["elementsViewId"] = Value(name == "Rectangle"      ? "rectangle"
                                            : name == "Ellipse"      ? "ellipse"
                                            : name == "Polygon"      ? "polygon"
                                            : name == "Line / Arrow" ? "line"
                                                                     : "inspect");
        }
        data_["inspectedElementId"] = selectedId;
        data_["inspectedElementShape"] = shape;
        return {};
    }
    case ToolAction::selectElementView: {
        for (const auto &view : array(get(catalog_.resource("elements"), "views")))
            if (text(get(view, "id")) == key) {
                data_["elementsViewId"] = Value(key);
                break;
            }
        return {};
    }
    case ToolAction::editElement:
        if (text(data_.at("toolId")) != "elements" || !elementFieldEnabled(key) || get(elementValues(), key) == arg(1))
            return {};
        return execute({ToolAction::setValue, {arg(0), arg(1)}});
    case ToolAction::resetElementView: {
        ToolReply result;
        const auto current = presentation();
        const auto active = get(current, "activeElementCandidate");
        if (active.kind() != ii::core::ValueKind::null && !boolean(get(active, "editable")))
            return {};
        auto draft = object(elementValues());
        for (const auto &field : array(get(current, "elementFields"))) {
            const auto name = text(field);
            const auto initial = get(read(Domain::elements, Read::field, {field}), "initial");
            if (get(Value(draft), name) == initial)
                continue;
            draft[name] = initial;
            result.edits.push_back({"elements", name, initial});
        }
        auto settings = object(data_.at("settingsByTool"));
        settings["elements"] = Value(std::move(draft));
        data_["settingsByTool"] = Value(std::move(settings));
        data_["notice"] = Value("");
        return result;
    }
    case ToolAction::commitElementNumber: {
        const auto index = static_cast<int>(number(arg(1)));
        auto input = trim(text(arg(2)));
        for (auto suffix : {"px", "%", "°"})
            if (input.ends_with(suffix))
                input = trim(input.substr(0, input.size() - std::string_view(suffix).size()));
        if (input.empty())
            return {};
        Value spec = read(Domain::elements, Read::field, {arg(0)});
        if (key == "field-0")
            spec = Value(Object{{"min", Value(1)}, {"max", Value(65536)}});
        else {
            auto limits = object(spec);
            limits["min"] = get(spec, "minimum");
            limits["max"] = get(spec, "maximum");
            spec = Value(limits);
        }
        const auto parsed = parseNumber(spec, input, true);
        if (!parsed.valid)
            return {};
        if (key != "field-0")
            return execute({ToolAction::editElement, {arg(0), parsed.value}});
        const auto all = elementValues();
        auto size = array(get(all, "field-0"));
        if (size.size() != 2 || index < 0 || index > 1)
            return {};
        const auto next = number(parsed.value);
        if (boolean(get(all, "field-3")))
            size[1 - index] =
                Value(std::max(1.0, std::floor(next * number(size[1 - index]) / number(size[index]) + .5)));
        size[index] = parsed.value;
        for (const auto &axis : size)
            if (number(axis) < 1 || number(axis) > 65536)
                return {};
        return execute({ToolAction::editElement, {arg(0), Value(size)}});
    }
    case ToolAction::selectCompactView: {
        const auto tool = text(data_.at("toolId"));
        const auto view = read(Domain::compact, Read::view, {Value(tool), arg(0)});
        if (view.kind() == ii::core::ValueKind::null)
            return {};
        auto next = object(data_.at("compactViewsByTool"));
        next[tool] = arg(0);
        data_["compactViewsByTool"] = Value(next);
        if (!text(get(view, "mode")).empty() && get(values(), "selector") != get(view, "mode"))
            return execute({ToolAction::setValue, {Value("selector"), get(view, "mode")}});
        return {};
    }
    case ToolAction::resetCompactView: {
        const auto view = get(presentation(), "compactSelectedView");
        auto result = execute({ToolAction::resetTool, {}});
        if (!text(get(view, "mode")).empty()) {
            const auto next = execute({ToolAction::setValue, {Value("selector"), get(view, "mode")}});
            result.edits = next.edits;
        }
        return result;
    }
    case ToolAction::editText: {
        if (text(data_.at("toolId")) != "text" || get(textValues(), key) == arg(1))
            return {};
        ToolReply result;
        const auto set = [&](std::string field, Value value) {
            const auto next = execute({ToolAction::setValue, {Value(field), value}});
            result.edits.insert(result.edits.end(), next.edits.begin(), next.edits.end());
        };
        set(key, arg(1));
        if (key == "field-5" || key.find("Constraint") != std::string::npos || key == "textReference" ||
            key == "textOffsetX" || key == "textOffsetY")
            set("textLayoutEnabled", Value(true));
        const auto value = text(arg(1));
        const auto placement = text(get(textValues(), "field-5"));
        if (key == "textHorizontalConstraint" && (value == "Left" || value == "Center" || value == "Right")) {
            const std::string vertical = placement.starts_with("Top")      ? "Top"
                                         : placement.starts_with("Bottom") ? "Bottom"
                                                                           : "";
            set("field-5", Value(vertical.empty() ? value : vertical + (value == "Center" ? "" : " " + lower(value))));
        }
        if (key == "textVerticalConstraint" && (value == "Top" || value == "Center" || value == "Bottom")) {
            const auto p = lower(placement);
            std::string horizontal = p.find("left") != std::string::npos    ? "left"
                                     : p.find("right") != std::string::npos ? "right"
                                                                            : "";
            if (value == "Center") {
                if (!horizontal.empty())
                    horizontal[0] = static_cast<char>(std::toupper(horizontal[0]));
                set("field-5", Value(horizontal.empty() ? "Center" : horizontal));
            } else
                set("field-5", Value(value + (horizontal.empty() ? "" : " " + horizontal)));
        }
        if (key == "field-2" && text(get(textValues(), "textLinePreset")) == "Token")
            set("textLineHeight", arg(1));
        if (key == "textLineHeight")
            set("textLinePreset", Value("Token"));
        return result;
    }
    case ToolAction::patchText:
    case ToolAction::resetTextView:
    case ToolAction::textRole: {
        Value patch = arg(0);
        if (command.action == ToolAction::textRole)
            patch = read(Domain::text, Read::roleValues, {arg(0)});
        if (command.action == ToolAction::resetTextView) {
            const auto index = static_cast<int>(number(arg(0)));
            patch = read(Domain::text, Read::defaultsFor,
                         {get(catalog_.resource("text"), index == 1   ? "typography"
                                                         : index == 2 ? "layout"
                                                                      : "appearance")});
        }
        ToolReply result;
        for (const auto &[field, value] : object(patch)) {
            const auto next = execute({ToolAction::editText, {Value(field), value}});
            result.edits.insert(result.edits.end(), next.edits.begin(), next.edits.end());
        }
        if (command.action == ToolAction::resetTextView && number(arg(0)) == 2) {
            const auto next = execute({ToolAction::editText, {Value("textLayoutEnabled"), Value(false)}});
            result.edits.insert(result.edits.end(), next.edits.begin(), next.edits.end());
        }
        return result;
    }
    case ToolAction::textLinePreset: {
        auto result = execute({ToolAction::editText, {Value("textLinePreset"), arg(0)}});
        double ratio = 1;
        if (key != "Token") {
            try {
                ratio = std::stod(key) / 100;
            } catch (...) {
                return result;
            }
        }
        const auto next = execute({ToolAction::setValue,
                                   {Value("textLineHeight"),
                                    Value(std::floor(number(get(textValues(), "field-2")) * ratio * 100 + .5) / 100)}});
        result.edits.insert(result.edits.end(), next.edits.begin(), next.edits.end());
        return result;
    }
    }
    return {};
}
} // namespace dreamscapes::vm
