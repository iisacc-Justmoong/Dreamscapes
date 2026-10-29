#include "GraphicDraft.h"
#include <regex>

namespace dreamscapes::vm {
namespace {
void visitRows(const Value &rows, Array &out) {
    for (const auto &row : array(rows)) {
        out.push_back(row);
        visitRows(get(row, "rows"), out);
    }
}
bool truthy(const Value &v) {
    if (v.kind() == ii::core::ValueKind::null)
        return false;
    if (v.kind() == ii::core::ValueKind::boolean)
        return boolean(v);
    if (v.kind() == ii::core::ValueKind::text)
        return !text(v).empty();
    if (v.kind() == ii::core::ValueKind::signed_integer || v.kind() == ii::core::ValueKind::unsigned_integer ||
        v.kind() == ii::core::ValueKind::real)
        return number(v) != 0;
    return true;
}
bool curve(const Value &item) {
    return (item.kind() == ii::core::ValueKind::array && array(item).size() == 2) ||
           (has(item, "input") && has(item, "output"));
}
} // namespace
GraphicDraft::GraphicDraft(ViewCatalog &catalog, ToolRead read, ToolWrite write)
    : catalog_(catalog), toolRead_(std::move(read)), toolWrite_(std::move(write)),
      commands_([this](const auto &command) { return execute(command); }),
      activations_([this](const auto &input) { return activate(input); }) {}
Value GraphicDraft::read(Read operation, std::vector<Value> args) const {
    const auto result = catalog_.queries(Domain::graphics).send({operation, std::move(args)});
    return result ? *result : Value();
}
Value GraphicDraft::toolValues() const {
    return get(toolRead_(), "values");
}
std::string GraphicDraft::defaultView() const {
    const auto tool = text(get(toolRead_(), "toolId"));
    const auto family = text(read(Read::normalizedTool, {Value(tool)}));
    const auto value = text(get(get(catalog_.resource("graphic"), "defaults"), family));
    const auto panels = viewCatalog();
    return !value.empty() ? value : array(panels).empty() ? "" : text(get(array(panels).front(), "id"));
}
Value GraphicDraft::selectedView() const {
    return read(Read::byId, {Value(viewId_)});
}
Value GraphicDraft::viewCatalog() const {
    return read(Read::panelsFor, {get(toolRead_(), "toolId")});
}
Value GraphicDraft::rowsOf(const Value &rows) const {
    Array result;
    visitRows(rows, result);
    return Value(result);
}
std::string GraphicDraft::operationFor(const Value &row) const {
    auto operation = text(get(row, "operation"));
    if (!operation.empty())
        return operation;
    const auto api = text(get(row, "api"));
    return truthy(get(row, "sourceArgument")) && !api.empty() ? api : text(get(selectedView(), "operation"));
}
Value GraphicDraft::parameters(std::string_view operation) const {
    const auto view = selectedView();
    const auto defaults =
        text(get(view, "operation")) == operation ? read(Read::defaultsFor, {Value(viewId_)}) : Value(Object{});
    return nested(merged(read(Read::operationDefaults, {Value(operation)}), defaults),
                  get(get(toolValues(), "graphicDrafts"), operation));
}
std::string GraphicDraft::parameterPath(const Value &row) const {
    auto path = resolvedPath(text(get(row, "sdkParameter"), text(get(row, "parameter"))), indices_);
    if (text(get(selectedView(), "name")).find("PointColor") != std::string::npos && !truthy(get(row, "collection"))) {
        const auto at = path.find("points[0]");
        if (at != std::string::npos)
            path.replace(at, 9, "points[" + std::to_string(static_cast<int>(number(get(indices_, "points")))) + ']');
    }
    return path;
}
std::optional<Value> GraphicDraft::value(const Value &row) const {
    if (boolean(get(row, "uiOnly")) && truthy(get(row, "editorTarget")))
        return Value("Sample " + std::to_string(static_cast<int>(number(get(indices_, "points"))) + 1));
    const auto draft = get(get(toolValues(), "graphicDrafts"), operationFor(row));
    const auto key = parameterPath(row);
    if (!key.empty() && has(draft, key))
        return get(draft, key);
    if (!key.empty() && !boolean(get(row, "uiOnly")))
        if (const auto found = pathValue(nested(draft), key))
            return found;
    if (truthy(get(row, "collection"))) {
        const auto definition = collectionDefinition();
        const auto path = text(get(definition, "path"));
        if (!path.empty() && key.starts_with(path + '['))
            if (const auto found = pathValue(collectionItems(), key.substr(path.size())))
                return found;
    }
    return has(row, "initial") ? std::optional<Value>(get(row, "initial")) : std::nullopt;
}
Value GraphicDraft::collectionDefinition() const {
    const auto view = selectedView();
    Value field;
    for (const auto &row : array(read(Read::flattenedRows, {Value(viewId_)})))
        if (truthy(get(row, "collection"))) {
            field = row;
            break;
        }
    if (field.kind() == ii::core::ValueKind::null)
        return {};
    const auto editor = read(Read::editorFor, {Value(viewId_)});
    return Value(Object{{"path", Value(resolvedPath(text(get(get(field, "collection"), "path")), indices_))},
                        {"minimum", Value(number(get(editor, "minItems")))},
                        {"maximum", Value(number(get(editor, "maxItems"), 65536))},
                        {"initial", has(editor, "initial") ? get(editor, "initial") : Value(Array{})}});
}
Value GraphicDraft::collectionItem() const {
    const auto definition = collectionDefinition();
    if (definition.kind() == ii::core::ValueKind::null)
        return Value(Object{});
    const auto prefix = text(get(definition, "path"));
    Value item(Object{});
    for (const auto &row : array(rowsOf(get(selectedView(), "rows")))) {
        if (!truthy(get(row, "collection")) || !has(row, "initial") || boolean(get(row, "uiOnly")) ||
            truthy(get(row, "sourceArgument")))
            continue;
        const auto path = resolvedPath(text(get(row, "parameter")), indices_);
        if (!path.starts_with(prefix + '['))
            continue;
        const auto bracket = path.find(']', prefix.size());
        if (bracket == std::string::npos)
            continue;
        auto suffix = path.substr(bracket + 1);
        if (suffix.starts_with('.'))
            suffix.erase(0, 1);
        if (!suffix.empty())
            item = withPath(item, suffix, get(row, "initial"));
    }
    return item;
}
Value GraphicDraft::collectionItems() const {
    const auto definition = collectionDefinition();
    if (definition.kind() == ii::core::ValueKind::null)
        return Value(Array{});
    const auto found = pathValue(parameters(text(get(selectedView(), "operation"))), text(get(definition, "path")));
    auto items = found ? array(*found) : Array{};
    if (items.empty() && number(get(definition, "minimum")) >= 2) {
        const auto item = collectionItem();
        if (item.kind() == ii::core::ValueKind::array && array(item).size() == 2)
            return Value(Array{Value(Array{Value(0), Value(0)}), Value(Array{Value(255), Value(255)})});
        if (has(item, "input") && has(item, "output"))
            return Value(Array{Value(Object{{"input", Value(0)}, {"output", Value(0)}}),
                               Value(Object{{"input", Value(1)}, {"output", Value(1)}})});
    }
    return Value(items);
}
bool GraphicDraft::selectView(std::string_view identifier) {
    const auto panel = read(Read::byId, {Value(identifier)});
    if (panel.kind() == ii::core::ValueKind::null)
        return false;
    auto tool = text(get(panel, "tool"));
    if (tool == "mask")
        tool = "masking";
    else if (tool == "adjust")
        tool = "auto-enhance";
    const auto state = toolRead_();
    auto views = object(get(state, "graphicViewsByTool"));
    views[tool] = get(panel, "id");
    toolWrite_({ToolAction::write, {Value("graphicViewsByTool"), Value(views)}});
    if (text(get(state, "toolId")) != tool)
        toolWrite_({ToolAction::write, {Value("toolId"), Value(tool)}});
    const auto next = text(get(panel, "operation"));
    if (next != indicesOperation_) {
        if (!indicesOperation_.empty())
            indicesByOperation_[indicesOperation_] = indices_;
        indices_ = indicesByOperation_.contains(next) ? indicesByOperation_.at(next) : Value(Object{});
        indicesOperation_ = next;
    }
    viewId_ = text(get(panel, "id"));
    return true;
}
void GraphicDraft::selectCollection(int index) {
    const auto definition = collectionDefinition();
    if (definition.kind() == ii::core::ValueKind::null)
        return;
    auto indices = object(indices_);
    const auto items = collectionItems();
    indices[text(get(definition, "path"))] =
        Value(std::clamp(index, 0, std::max(0, static_cast<int>(array(items).size()) - 1)));
    indices_ = Value(indices);
}
bool GraphicDraft::execute(const GraphicCommand &command) {
    static const Value empty;
    const auto arg = [&](std::size_t i) -> const Value & {
        return i < command.arguments.size() ? command.arguments[i] : empty;
    };
    switch (command.action) {
    case GraphicAction::restoreView: {
        const auto state = toolRead_();
        auto saved = text(get(get(state, "graphicViewsByTool"), text(get(state, "toolId"))));
        if (read(Read::byId, {Value(saved)}).kind() == ii::core::ValueKind::null)
            saved = defaultView();
        return selectView(saved);
    }
    case GraphicAction::selectView:
        return selectView(text(arg(0)));
    case GraphicAction::selectCollection:
        selectCollection(static_cast<int>(number(arg(0))));
        return true;
    case GraphicAction::setIndices:
        indices_ = arg(0);
        return true;
    case GraphicAction::addCollectionItem: {
        const auto definition = collectionDefinition();
        if (definition.kind() == ii::core::ValueKind::null)
            return false;
        auto items = array(collectionItems());
        if (items.size() >= number(get(definition, "maximum")))
            return false;
        int selected = static_cast<int>(items.size());
        const auto item = collectionItem();
        if (curve(item) && items.size() >= 2) {
            const auto x = [](const Value &p) {
                return p.kind() == ii::core::ValueKind::array ? number(array(p)[0]) : number(get(p, "input"));
            };
            const auto y = [](const Value &p) {
                return p.kind() == ii::core::ValueKind::array ? number(array(p)[1]) : number(get(p, "output"));
            };
            double gap = -1;
            std::size_t insert = 1;
            for (std::size_t i = 1; i < items.size(); ++i)
                if (x(items[i]) - x(items[i - 1]) > gap) {
                    gap = x(items[i]) - x(items[i - 1]);
                    insert = i;
                }
            auto input = (x(items[insert - 1]) + x(items[insert])) / 2;
            if (item.kind() == ii::core::ValueKind::array)
                input = std::floor(input);
            if (input <= x(items[insert - 1]) || input >= x(items[insert]))
                return false;
            const auto output = (y(items[insert - 1]) + y(items[insert])) / 2;
            items.insert(items.begin() + insert,
                         item.kind() == ii::core::ValueKind::array
                             ? Value(Array{Value(input), Value(std::floor(output + .5))})
                             : Value(Object{{"input", Value(input)}, {"output", Value(output)}}));
            selected = static_cast<int>(insert);
        } else if (items.empty() && !array(get(definition, "initial")).empty()) {
            items = array(get(definition, "initial"));
            selected = 0;
        } else
            items.push_back(item);
        toolWrite_({ToolAction::replaceGraphicCollection,
                    {get(selectedView(), "operation"), get(definition, "path"), Value(items)}});
        selectCollection(selected);
        return true;
    }
    case GraphicAction::removeCollectionItem: {
        const auto definition = collectionDefinition();
        auto items = array(collectionItems());
        if (definition.kind() == ii::core::ValueKind::null || items.size() <= number(get(definition, "minimum")))
            return false;
        const auto index = std::clamp(static_cast<int>(number(get(indices_, text(get(definition, "path"))))), 0,
                                      static_cast<int>(items.size()) - 1);
        items.erase(items.begin() + index);
        toolWrite_({ToolAction::replaceGraphicCollection,
                    {get(selectedView(), "operation"), get(definition, "path"), Value(items)}});
        selectCollection(index);
        return true;
    }
    case GraphicAction::edit: {
        const auto &row = arg(0);
        if (text(get(row, "parameter")).empty())
            return false;
        if (boolean(get(row, "uiOnly"))) {
            if (truthy(get(row, "editorTarget"))) {
                auto indices = object(indices_);
                auto label = text(arg(1));
                if (label.starts_with("Sample "))
                    label.erase(0, 7);
                double n = 0;
                try {
                    n = std::stod(label);
                } catch (...) {
                }
                indices["points"] = Value(std::max(0.0, n - 1));
                indices_ = Value(indices);
            }
            return true;
        }
        if (value(row) == std::optional<Value>(arg(1)))
            return true;
        if (truthy(get(row, "collection"))) {
            const auto definition = collectionDefinition();
            auto items = array(collectionItems());
            if (definition.kind() == ii::core::ValueKind::null)
                return false;
            const auto path = text(get(definition, "path"));
            const auto index = static_cast<std::size_t>(std::max(0.0, number(get(indices_, path))));
            if (index > 65535)
                return false;
            while (items.size() <= index)
                items.push_back(collectionItem());
            if (items.size() < number(get(definition, "minimum")) &&
                array(get(definition, "initial")).size() >= number(get(definition, "minimum")))
                items = array(get(definition, "initial"));
            const auto fullPath = parameterPath(row);
            const auto suffix = fullPath.substr(path.size());
            const auto updated = withPath(Value(items), suffix, arg(1));
            toolWrite_(
                {ToolAction::replaceGraphicCollection, {get(selectedView(), "operation"), Value(path), updated}});
        } else
            toolWrite_({ToolAction::setGraphicValue, {Value(operationFor(row)), Value(parameterPath(row)), arg(1)}});
        return true;
    }
    case GraphicAction::resetView: {
        const auto view = selectedView();
        if (view.kind() == ii::core::ValueKind::null)
            return false;
        auto defaults = object(read(Read::defaultsFor, {Value(viewId_)}));
        for (const auto &row : array(rowsOf(get(view, "rows"))))
            if (truthy(get(row, "collection")) && has(row, "initial") && !boolean(get(row, "uiOnly")) &&
                !truthy(get(row, "sourceArgument")))
                defaults[parameterPath(row)] = get(row, "initial");
        toolWrite_({ToolAction::resetGraphicView, {get(view, "operation"), Value(defaults)}});
        return true;
    }
    case GraphicAction::applyReturnedParameters: {
        const auto &parameters = get(arg(1), "parameters");
        if (object(parameters).empty())
            return false;
        const auto api = text(arg(0));
        const auto target = api == "autoDevelopLight" ? "developLight"
                            : api == "autoWhiteBalance" || api == "sampledWhiteBalance" || api == "whiteBalancePreset"
                                ? "whiteBalance"
                            : api == "autoBlackAndWhiteMix" || api == "samplePointColor" ? "developColor"
                            : api == "estimateChromaticAberration"                       ? "correctOptics"
                                                                   : text(get(selectedView(), "operation"));
        const auto prefix = api == "samplePointColor"
                                ? "points[" + std::to_string(static_cast<int>(number(get(indices_, "points")))) + ']'
                                : "";
        toolWrite_({ToolAction::setGraphicValues, {Value(target), flatten(parameters, prefix)}});
        return true;
    }
    }
    return false;
}
Value GraphicDraft::curvePoints(const Value &row) const {
    const auto view = selectedView();
    Value input;
    static const std::regex inputPath(R"(\.(input)$|\[\]\[0\])");
    for (const auto &field : array(rowsOf(get(view, "rows"))))
        if (std::regex_search(text(get(field, "parameter")), inputPath)) {
            input = field;
            break;
        }
    const auto fallback = has(row, "points")
                              ? get(row, "points")
                              : Value(Array{Value(Array{Value(0), Value(0)}), Value(Array{Value(1), Value(1)})});
    if (input.kind() == ii::core::ValueKind::null)
        return fallback;
    auto path = std::regex_replace(text(get(input, "parameter")), std::regex(R"(\[\d*\]\.input$|\[\]\[0\]$)"), "");
    path = resolvedPath(path, indices_);
    const auto points = pathValue(parameters(text(get(view, "operation"))), path);
    if (!points || array(*points).size() < 2)
        return fallback;
    const auto scale = number(get(input, "max")) == 255 ? 255 : 1;
    Array result;
    for (const auto &p : array(*points)) {
        if (p.kind() == ii::core::ValueKind::array && array(p).size() >= 2)
            result.emplace_back(Array{Value(number(array(p)[0]) / scale), Value(number(array(p)[1]) / scale)});
        else
            result.emplace_back(Array{Value(number(get(p, "input")) / scale), Value(number(get(p, "output")) / scale)});
    }
    return Value(result);
}
Value GraphicDraft::statusFor(std::string_view status) const {
    auto family = text(read(Read::normalizedTool, {get(toolRead_(), "toolId")}));
    if (family == "mask")
        family = "adjust";
    return status.empty()
               ? Value()
               : read(Read::byId, {get(get(get(catalog_.resource("graphic"), "statusViews"), family), status)});
}
GraphicPlan GraphicDraft::activate(const GraphicActivation &input) const {
    const auto &option = input.option, &row = input.row;
    GraphicPlan result;
    result.row = row;
    const auto first = [](std::initializer_list<Value> values) {
        for (const auto &value : values)
            if (truthy(value))
                return text(value);
        return std::string();
    };
    const auto action = first({get(option, "action"), get(option, "value"), get(row, "action")});
    if (action == "preview" || action == "apply") {
        result.route = GraphicRoute::execute;
        result.action = action;
        return result;
    }
    if (action == "reset") {
        result.route = GraphicRoute::reset;
        return result;
    }
    if (action == "overview") {
        const auto overview = read(Read::overviewFor, {get(toolRead_(), "toolId")});
        result.route = GraphicRoute::view;
        result.target =
            first({get(option, "target"), get(selectedView(), "parent"), get(overview, "id"), Value(defaultView())});
        return result;
    }
    if (!text(get(option, "target")).empty()) {
        result.route = GraphicRoute::view;
        result.target = text(get(option, "target"));
        return result;
    }
    result.operation = first({get(option, "api"), get(row, "api")});
    if (boolean(get(row, "uiOnly")) && result.operation == "whiteBalancePreset") {
        if (has(option, "value") && get(option, "value").kind() != ii::core::ValueKind::null) {
            result.route = GraphicRoute::execute;
            result.action = "helper";
            result.parameters = Value(Object{{"preset", get(option, "value")}});
        }
        return result;
    }
    if (text(get(row, "editor")) == "hostImagePicker" || text(get(row, "inputType")) == "image" ||
        text(get(row, "inputType")) == "file") {
        result.route = GraphicRoute::source;
        return result;
    }
    if (boolean(get(row, "sourceArgument")) && text(get(row, "parameter")) == "sample") {
        result.route = GraphicRoute::sample;
        return result;
    }
    if (text(get(row, "editor")) == "hostColorPicker") {
        result.route = GraphicRoute::color;
        if (!boolean(get(row, "sourceArgument")))
            result.operation.clear();
        auto current = pathValue(parameters(operationFor(row)), parameterPath(row));
        result.value = current && truthy(*current) ? *current : value(row).value_or(Value());
        return result;
    }
    if (boolean(get(row, "sourceArgument"))) {
        result.route = GraphicRoute::execute;
        result.action = "helper";
        result.parameters = Value(Object{});
        if (result.operation == "combineMasks") {
            result.action = "mask-helper";
            result.parameters = Value(Object{{"operation", get(option, "value")}});
        } else if (result.operation == "invertMask")
            result.action = "mask-helper";
        else if (text(get(row, "parameter")) == "options") {
            result.action = "preview";
            const auto points = get(parameters(text(get(selectedView(), "operation"))), "points");
            const auto index = static_cast<int>(number(get(indices_, "points")));
            if (index >= 0 && index < array(points).size())
                result.parameters = array(points)[index];
        }
        return result;
    }
    if (has(row, "parameter") && has(option, "value")) {
        result.route = GraphicRoute::edit;
        result.value = get(option, "value");
    }
    return result;
}
} // namespace dreamscapes::vm
