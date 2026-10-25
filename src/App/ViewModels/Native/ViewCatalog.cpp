#include "ViewCatalog.h"
#include <regex>
#include <set>
#include <sstream>
#include <iiSDKCore/Core/Text/Text.h>

namespace dreamscapes::vm {
namespace {
Value findBy(const Value &values, std::string_view key, std::string_view expected, bool fallback = false) {
    for (const auto &entry : array(values)) if (text(get(entry, key)) == expected) return entry;
    return fallback && !array(values).empty() ? array(values).front() : Value();
}
Value fieldsDefault(const Value &fields, Value::Object result = {}) {
    for (const auto &field : array(fields)) if (has(field, "initial")) result[text(get(field, "id"))] = get(field, "initial");
    return Value(std::move(result));
}
std::string normalized(std::string value) { return value == "masking" ? "mask" : value == "auto-enhance" ? "adjust" : value; }
bool documentDefault(const Value &row) {
    const auto parameter = text(get(row, "parameter")), editor = text(get(row, "editor"));
    return !parameter.empty() && parameter.find("[]") == std::string::npos && has(row, "initial")
        && !boolean(get(row, "uiOnly")) && !boolean(get(row, "sourceArgument")) && text(get(row, "type")) != "curve"
        && editor != "hostColorPicker" && editor != "hostImagePicker" && !(editor == "imageInput" && get(row, "initial").kind() == ii::core::ValueKind::text);
}
void collectDefaults(const Value &rows, Object &result, bool firstOnly) {
    for (const auto &row : array(rows)) {
        const auto key = text(get(row, "sdkParameter"), text(get(row, "parameter")));
        if (documentDefault(row) && (!firstOnly || !result.contains(key))) result[key] = get(row, "initial");
        collectDefaults(get(row, "rows"), result, firstOnly);
    }
}
std::string scalar(const Value &value) {
    if (value.kind() == ii::core::ValueKind::text) return text(value);
    if (value.kind() == ii::core::ValueKind::boolean) return boolean(value) ? "true" : "false";
    if (value.kind() == ii::core::ValueKind::null) return "";
    std::ostringstream out; out.precision(15); out << number(value); return out.str();
}
const Value &metadata(const Value &file, std::string_view key) {
    if (has(file, key) && get(file, key).kind() != ii::core::ValueKind::null) return get(file, key);
    for (auto container : {"generationParameters", "parameters", "metadata"}) if (has(file, container)) return get(get(file, container), key);
    return get(file, key);
}
std::string kind(const Value &file) {
    if (!text(get(file, "mediaType")).empty()) return text(get(file, "mediaType"));
    const auto name = lower(text(get(file, "name")));
    if (std::regex_search(name, std::regex(R"(\.iiscp?$)"))) return "Canvas";
    if (std::regex_search(name, std::regex(R"(\.(mp4|mov|m4v|webm|mkv)$)"))) return "Video";
    return boolean(get(file, "isImage")) ? "Image" : "Document";
}
std::string dimensions(const Value &file) {
    const auto w = number(metadata(file, "width")), h = number(metadata(file, "height"));
    return w > 0 && h > 0 ? scalar(Value(w)) + " × " + scalar(Value(h)) + " px" : "Unavailable";
}
std::string structure(const Value &file) {
    const auto layers = number(metadata(file, "layerCount")), pages = number(metadata(file, "pageCount"));
    if (layers > 0) return (pages > 0 ? scalar(Value(pages)) + " pages · " : "") + scalar(Value(layers)) + " layers";
    const auto duration = number(metadata(file, "duration")), fps = number(metadata(file, "fps"));
    if (duration > 0) return scalar(Value(duration)) + " s" + (fps > 0 ? " · " + scalar(Value(fps)) + " FPS" : "");
    return text(get(file, "metadata1"), kind(file));
}
}

ViewCatalog::ViewCatalog(std::map<std::string, Value, std::less<>> resources) : resources_(std::move(resources)) {
    for (const auto &entry : array(resource("tools"))) toolById_[text(get(entry, "key"))] = entry;
    for (const auto &name : {"panels", "overviews"}) for (const auto &entry : array(get(resource("graphic"), name))) {
        graphicById_.try_emplace(text(get(entry, "id")), entry);
        graphicById_.try_emplace(text(get(entry, "name")), entry);
    }
    for (const auto &entry : array(get(resource("graphic"), "panels"))) {
        const auto operation = text(get(entry, "operation"));
        auto values = object(operationDefaults_[operation]);
        collectDefaults(get(entry, "rows"), values, true);
        operationDefaults_[operation] = Value(std::move(values));
    }
    for (auto domain : {Domain::tools, Domain::graphics, Domain::graphicState, Domain::file, Domain::elements,
         Domain::text, Domain::photo, Domain::compact, Domain::viewOptions, Domain::selection, Domain::storage, Domain::facts})
        queries_[domain] = std::make_unique<ii::runtime::QueryPort<ViewRead, Value>>([this, domain](const ViewRead &query) { return read(domain, query); });
}
const ii::runtime::QueryPort<ViewRead, Value> &ViewCatalog::queries(Domain domain) const { return *queries_.at(domain); }
const Value &ViewCatalog::resource(std::string_view name) const {
    static const Value empty;
    const auto found = resources_.find(name);
    return found == resources_.end() ? empty : found->second;
}
Value ViewCatalog::tool(std::string_view key) const {
    const auto found = toolById_.find(key);
    return found == toolById_.end() ? Value() : found->second;
}
Value ViewCatalog::graphic(std::string_view key) const {
    const auto found = graphicById_.find(key);
    return found == graphicById_.end() ? Value() : found->second;
}
Value ViewCatalog::graphicDefaults(std::string_view identifier, bool operation) const {
    if (operation) {
        const auto found = operationDefaults_.find(identifier);
        return found == operationDefaults_.end() ? Value(Object{}) : found->second;
    }
    auto panel = graphic(identifier);
    if (panel.kind() == ii::core::ValueKind::null) panel = graphic(text(get(get(resource("graphic"), "defaults"), normalized(std::string(identifier)))));
    Object result; collectDefaults(get(panel, "rows"), result, false); return Value(std::move(result));
}
Value ViewCatalog::read(Domain domain, const ViewRead &query) const {
    static const Value empty;
    const auto arg = [&](std::size_t i) -> const Value & { return i < query.arguments.size() ? query.arguments[i] : empty; };
    const auto key = text(arg(0));
    const auto domainResource = domain == Domain::file ? "file" : domain == Domain::photo ? "photo" : domain == Domain::text ? "text" : "elements";
    const auto views = get(resource(domainResource), "views");
    switch (query.operation) {
    case Read::tool: return domain == Domain::compact ? get(get(resource("compact"), "tools"), key) : tool(key);
    case Read::catalog: return resource("graphic");
    case Read::normalizedTool: return Value(normalized(key));
    case Read::panelsFor: {
        Array result; for (const auto &entry : array(get(resource("graphic"), "panels"))) if (text(get(entry, "tool")) == normalized(key)) result.push_back(entry);
        return Value(std::move(result));
    }
    case Read::byId: return domain == Domain::text ? findBy(get(resource("text-definition"), "fields"), "id", key) : graphic(key);
    case Read::overviewFor: return findBy(get(resource("graphic"), "overviews"), "tool", normalized(key));
    case Read::operationDefaults: return graphicDefaults(key, true);
    case Read::defaultsFor: return domain == Domain::text ? fieldsDefault(arg(0)) : graphicDefaults(key, false);
    case Read::views: return domain == Domain::compact ? get(get(get(resource("compact"), "tools"), key), "views") : views;
    case Read::view:
        return domain == Domain::compact ? findBy(get(get(get(resource("compact"), "tools"), key), "views"), "id", text(arg(1))) : findBy(views, "id", key, true);
    case Read::definition: {
        if (domain == Domain::text) return resource("text-definition");
        if (domain == Domain::file) return Value(Object{{"key", Value("file")}, {"title", Value("File")}, {"number", Value(5)}, {"figmaNode", array(views).empty() ? Value() : get(array(views)[0], "figmaNode")}, {"views", views}, {"fields", Value(Array{})}});
        if (domain == Domain::photo) return tool("camera-photo");
        if (domain == Domain::graphics) {
            auto panel = graphic(text(get(get(resource("graphic"), "defaults"), normalized(key))));
            if (panel.kind() == ii::core::ValueKind::null) return {};
            auto name = normalized(key); if (!name.empty()) name[0] = static_cast<char>(std::toupper(name[0]));
            return Value(Object{{"key", Value(key)}, {"name", Value(name)}, {"title", get(panel, "title")}, {"figmaNode", get(panel, "id")}, {"fields", Value(Array{})}});
        }
        return tool(key);
    }
    case Read::field: return findBy(domain == Domain::elements ? get(tool("elements"), "fields") : get(resource("photo"), "fields"), "id", key);
    case Read::defaults: {
        if (domain == Domain::photo) return fieldsDefault(get(resource("photo"), "fields"));
        auto result = object(get(arg(0), "initialValues"));
        if (has(arg(0), "selector")) result["selector"] = get(get(arg(0), "selector"), "initial");
        return fieldsDefault(get(arg(0), "fields"), std::move(result));
    }
    case Read::runtimeDefinition: {
        if (arg(0).kind() == ii::core::ValueKind::null) return {};
        auto result = object(arg(0)); const auto id = text(get(arg(0), "key"));
        if (has(arg(0), "selector")) {
            auto selector = object(get(arg(0), "selector")); Array options;
            for (const auto &option : array(get(get(arg(0), "selector"), "options"))) {
                const auto name = text(option);
                if ((id == "camera-photo" && name == "RAW Capture") || (id == "layers" && (name == "Dynamic" || name == "Group")) || (id == "retouch" && name == "Face Refine")) continue;
                options.push_back(option);
            }
            selector["options"] = Value(options); result["selector"] = Value(selector);
        }
        Array fields;
        for (const auto &field : array(get(arg(0), "fields"))) {
            auto next = object(field); const auto fid = text(get(field, "id"));
            const auto set = [&](std::string k, Value value) { next[std::move(k)] = std::move(value); };
            if (id == "effects" && fid == "field-45") next = object(merged(field, Value(Object{{"type", Value("Slider")}, {"label", Value("Manual distortion")}, {"initial", Value(0)}, {"minimum", Value(-100)}, {"maximum", Value(100)}, {"step", Value(1)}, {"unit", Value("%")}, {"decimals", Value(0)}})));
            if (id == "effects" && fid == "field-46") { set("label", Value("Reduce color fringe")); set("initial", Value(false)); set("description", Value("수동으로 색수차를 줄인다")); }
            if (id == "layers" && fid == "field-19") { set("label", Value("Layer name")); set("initial", Value("")); }
            if (id == "text" && fid == "field-2") set("unit", Value("px"));
            if (id == "generative" && fid == "field-22") set("label", Value("Choose reference…"));
            if (id == "effects" && fid == "field-32") { set("options", Value(Array{Value("Uniform"), Value("Film")})); set("initial", Value("Uniform")); }
            if (id == "masking" && fid == "field-14") { set("options", Value(Array{Value("Sky"), Value("Background")})); set("initial", Value("Sky")); }
            if (id == "masking" && fid == "field-15") set("label", Value("Semantic region preview"));
            if (id == "retouch" && fid == "field-14") { set("label", Value("Follow stroke")); set("description", Value("스트로크 내 소스 오프셋을 유지한다")); }
            if (id == "select" && fid == "field-7") { set("label", Value("Snap to 8 px grid")); set("description", Value("8픽셀 격자에 맞춘다")); }
            fields.emplace_back(std::move(next));
        }
        result["fields"] = Value(fields); return Value(result);
    }
    case Read::applicableFields: {
        const auto &groups = get(resource("field-groups"), text(get(arg(0), "key")));
        if (groups.kind() == ii::core::ValueKind::null || !has(arg(0), "selector")) return get(arg(0), "fields");
        const auto mode = text(get(arg(1), "selector"), text(get(get(arg(0), "selector"), "initial")));
        auto allowed = array(get(groups, "common")); const auto &extra = array(get(groups, mode)); allowed.insert(allowed.end(), extra.begin(), extra.end());
        Array result; for (const auto &field : array(get(arg(0), "fields"))) {
            const auto id = text(get(field, "id")); int index = -1; try { index = std::stoi(id.substr(6)); } catch (...) {}
            if (std::ranges::any_of(allowed, [&](const auto &v) { return number(v, -2) == index; })) result.push_back(field);
        } return Value(result);
    }
    case Read::rows: {
        Array result; bool pending = false;
        for (const auto &field : array(get(arg(0), "fields"))) {
            const auto type = text(get(field, "type")); if (type == "Action") continue;
            const auto label = ii::core::to_utf16(text(get(field, "label")));
            const bool half = !boolean(arg(1)) && (type == "Slider" || type == "Toggle") && label && label->size() <= 25;
            if (half && pending) { auto row = array(result.back()); row.push_back(field); result.back() = Value(row); pending = false; }
            else { result.emplace_back(Array{field}); pending = half; }
        } return Value(result);
    }
    case Read::shapeView: return get(findBy(get(resource("elements"), "shapes"), "name", key, true), "id");
    case Read::controls: {
        if (key == "inspect") {
            const auto shape = text(arg(1));
            if (shape == "Ellipse") return Value(Array{Value("field-0"), Value("field-4"), Value("field-5"), Value("field-16"), Value("field-10")});
            if (shape == "Polygon") return Value(Array{Value("field-0"), Value("field-7"), Value("field-9"), Value("field-8"), Value("field-16"), Value("field-10")});
            if (shape == "Line / Arrow") return Value(Array{Value("field-0"), Value("field-10"), Value("field-17")});
            if (shape == "Vector") return Value(Array{Value("field-0"), Value("field-14"), Value("field-15")});
        } return get(findBy(views, "id", key, true), "fields");
    }
    case Read::roleValues: return Value(Object{{"textRole", get(arg(0), "name")}, {"field-2", get(arg(0), "size")}, {"textWeight", get(arg(0), "weight")}, {"textLineHeight", get(arg(0), "size")}, {"textLinePreset", Value("Token")}});
    case Read::readout: return Value(text(get(arg(0), "textRole")) + " · " + scalar(get(arg(0), "field-2")) + " px · " + text(get(arg(0), "textWeight")));
    case Read::fillColor: return Value(key == "Black" ? "#000000" : key == "Accent" ? "#0A84FF" : key == "Custom" ? text(arg(1)) : "#FFFFFF");
    case Read::sections: {
        const auto &v = arg(1);
        const auto lookup = key + '|' + text(get(v, "type"), "Single") + '|' + text(get(v, "units"), "px") + '|' + text(get(v, "orientation"), "Horizontal");
        const auto &data = resource("view-options");
        const auto &index = get(data, "index");
        if (!has(index, lookup)) return Value(Array{});
        const auto n = static_cast<std::size_t>(number(get(index, lookup)));
        return n < array(get(data, "sections")).size() ? array(get(data, "sections"))[n] : Value(Array{});
    }
    case Read::source: return imageSource(arg(0));
    case Read::key:
        if (domain == Domain::selection) return imageKey(arg(0));
        for (auto name : {"id", "key", "path", "source"}) if (!text(get(arg(0), name)).empty()) return get(arg(0), name);
        return Value("");
    case Read::isImage: return Value(isImage(arg(0)));
    case Read::historyImages: return historyImages(arg(0));
    case Read::kind: return Value(kind(arg(0)));
    case Read::dimensions: return Value(dimensions(arg(0)));
    case Read::structure: return Value(structure(arg(0)));
    case Read::date: {
        auto date = text(get(arg(0), "dateText")); if (!date.empty()) return Value(date);
        date = text(get(arg(0), "modified")); return Value(date.size() >= 10 && date[4] == '-' && date[7] == '-' ? date.substr(0, 10) : "Unavailable");
    }
    case Read::state: return Value(has(arg(0), "available") && !boolean(get(arg(0), "available"), true) ? "Not downloaded" : "Local");
    case Read::model: { auto result = scalar(metadata(arg(0), "modelName")); if (result.empty()) result = scalar(metadata(arg(0), "model")); return Value(result.empty() ? "Unavailable" : result); }
    case Read::resultSummary: return Value(dimensions(arg(0)) + " · " + (metadata(arg(0), "seed").kind() != ii::core::ValueKind::null ? "Seed " + scalar(metadata(arg(0), "seed")) : structure(arg(0))));
    case Read::sectionFiles: return sectionFiles(arg(0), text(arg(1)));
    case Read::listing: return storageListing(arg(0), text(arg(1)), text(arg(2)), text(arg(3)), text(arg(4)));
    }
    return {};
}

Value imageSource(const Value &entry) {
    for (auto key : {"documentSource", "source", "path", "imageSource", "mediaSource"}) if (!text(get(entry, key)).empty()) return get(entry, key);
    return Value("");
}
Value imageKey(const Value &entry) {
    const auto container = text(get(entry, "containerId")), key = text(get(entry, "key"));
    return !container.empty() && !key.empty() ? Value(container + '|' + key) : imageSource(entry);
}
bool isImage(const Value &entry) {
    const auto type = text(get(entry, "mediaType"));
    return type != "Video" && type != "Canvas" && (boolean(get(entry, "isImage"), true) || !text(get(entry, "documentSource")).empty())
        && !text(imageKey(entry)).empty() && (!text(get(entry, "documentSource")).empty() || !std::regex_search(text(imageSource(entry)), std::regex(R"(\.iiscp?(?:\?.*)?$)", std::regex::icase)));
}
Value historyImages(const Value &entries) {
    static const std::regex wrapper(R"(\.(png|jpg|jpeg|webp|bmp|tif|tiff)\.[0-9a-f]{64}\.iisc$)", std::regex::icase), suffix(R"(\.[0-9a-f]{64}\.iisc$)", std::regex::icase);
    std::set<std::string> originals; for (const auto &entry : array(entries)) originals.insert(text(get(entry, "path")));
    Array result; for (const auto &entry : array(entries)) {
        const auto path = text(get(entry, "path"));
        if (!std::regex_search(path, wrapper) || !originals.contains(std::regex_replace(path, suffix, ""))) result.push_back(entry);
    } return Value(result);
}
Value sectionFiles(const Value &aliases, std::string_view section) {
    static const std::regex wrapper(R"(\.(png|jpg|jpeg|webp|bmp|tif|tiff)\.[0-9a-f]{64}\.iisc$)", std::regex::icase), suffix(R"(\.[0-9a-f]{64}\.iisc$)", std::regex::icase);
    std::map<std::string, Value, std::less<>> paths;
    for (const auto &row : array(aliases)) if (text(get(row, "section")) == section && boolean(get(row, "exists"))) paths[text(get(row, "relativePath"))] = row;
    Array result;
    for (const auto &row : array(aliases)) {
        if (text(get(row, "section")) != section || !boolean(get(row, "exists"))) continue;
        const auto path = text(get(row, "relativePath")), name = text(get(row, "name"));
        if (section == "generation-history" && std::regex_search(path, wrapper) && paths.contains(std::regex_replace(path, suffix, ""))) continue;
        if (lower(path).ends_with("-poster.png") && paths.contains(path.substr(0, path.size() - 11) + "-0001.mp4")) continue;
        auto file = object(row);
        if (kind(row) == "Video") {
            file["mediaType"] = Value("Video"); file["mediaSource"] = get(row, "source");
            const auto posterPath = lower(path).ends_with("-0001.mp4") ? path.substr(0, path.size() - 9) + "-poster.png" : path;
            const auto poster = paths.find(posterPath);
            if (poster != paths.end() && posterPath != path) file["previewSource"] = get(poster->second, "previewSource");
            file["imageSource"] = file.contains("previewSource") ? file["previewSource"] : Value("");
        } else if (kind(row) == "Canvas") file["mediaType"] = Value("Canvas");
        result.emplace_back(file);
    } return Value(result);
}
Value storageListing(const Value &files, std::string_view section, std::string_view directory, std::string query, std::string_view sort) {
    const auto prefix = directory.empty() ? std::string() : std::string(directory) + '/';
    query = lower(trim(std::move(query)));
    std::set<std::string> folders; Array result;
    for (const auto &file : array(files)) {
        const auto relative = text(get(file, "relativePath")); if (!relative.starts_with(prefix)) continue;
        const auto tail = relative.substr(prefix.size()); const auto at = tail.find('/');
        if (!query.empty()) { if (lower(tail).find(query) != std::string::npos) result.push_back(file); }
        else if (at != std::string::npos) {
            const auto name = tail.substr(0, at), path = prefix + name;
            if (folders.insert(name).second) result.emplace_back(Object{{"key", Value("directory:" + std::string(section) + '/' + path)}, {"name", Value(name)}, {"relativePath", Value(path)}, {"isDirectory", Value(true)}, {"available", Value(true)}});
        } else result.push_back(file);
    }
    std::stable_sort(result.begin(), result.end(), [&](const auto &a, const auto &b) {
        const bool ad = boolean(get(a, "isDirectory")), bd = boolean(get(b, "isDirectory")); if (ad != bd) return ad;
        if (!ad && sort != "name") {
            const auto left = text(get(a, "modified")), right = text(get(b, "modified")); if (left != right) return sort == "oldest" ? left < right : left > right;
        }
        return lower(text(get(a, "name"))) < lower(text(get(b, "name")));
    });
    return Value(result);
}
} // namespace dreamscapes::vm
