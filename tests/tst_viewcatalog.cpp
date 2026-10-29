#include "App/ViewModels/Native/ViewCatalog.h"
#include <iiJson/Json.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace dreamscapes::vm;
using Resources = std::map<std::string, Value, std::less<>>;
using Clock = std::chrono::steady_clock;
namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
Resources resources() {
    Resources result;
    for (const auto *name : {"tools", "text", "text-definition", "photo", "file", "compact", "elements",
                             "graphic", "view-options", "field-groups", "background-defaults"}) {
        std::ifstream file(std::string(DREAMSCAPES_TEST_SOURCE_DIR) + "/resources/viewmodels/" + name + ".json");
        require(file.good(), "missing source catalog fixture");
        const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        ii::json::Limits limits; limits.items = 500000;
        auto parsed = ii::json::parse(bytes, limits);
        require(bool(parsed), "invalid source catalog fixture");
        result.emplace(name, std::move(*parsed));
    }
    return result;
}
Value query(ViewCatalog &catalog, Domain domain, Read read, std::vector<Value> arguments = {}) {
    auto reply = catalog.queries(domain).send({read, std::move(arguments)});
    require(bool(reply), "catalog query failed");
    return std::move(*reply);
}
void aliasesAndDetachedReplies(ViewCatalog &catalog) {
    const auto &panels = array(get(catalog.resource("graphic"), "panels"));
    require(panels.size() == 381, "graphic panel inventory changed");
    for (const auto &panel : panels) {
        const auto id = get(panel, "id"), name = get(panel, "name");
        require(query(catalog, Domain::graphics, Read::byId, {id}) == panel, "panel ID lookup changed");
        require(query(catalog, Domain::graphics, Read::byId, {name}) == panel, "panel name alias changed");
    }
    const auto &first = panels.front();
    const auto changed = withPath(query(catalog, Domain::graphics, Read::byId, {get(first, "id")}),
                                  "title", Value("independent caller edit"));
    require(text(get(changed, "title")) == "independent caller edit", "caller snapshot could not be edited");
    require(query(catalog, Domain::graphics, Read::byId, {get(first, "id")}) == first,
            "editing a query reply mutated the catalog");
}
void demandLoading(const Resources &fixture) {
    std::map<std::string, int, std::less<>> loads;
    ViewCatalog catalog({}, [&](std::string_view name) -> std::optional<Value> {
        ++loads[std::string(name)];
        const auto found = fixture.find(name);
        return found == fixture.end() ? std::nullopt : std::optional<Value>(found->second);
    });
    require(loads.empty(), "construction loaded unrelated catalogs");
    const Value image(Object{{"imageSource", Value("file:///unchanged.png")}});
    require(text(query(catalog, Domain::facts, Read::source, {image})) == "file:///unchanged.png",
            "resource-free source projection changed");
    require(text(query(catalog, Domain::graphics, Read::normalizedTool, {Value("masking")})) == "mask",
            "resource-free tool normalization changed");
    require(loads.empty(), "resource-free projection loaded catalogs");

    const auto &panels = array(get(fixture.at("graphic"), "panels"));
    const auto id = get(panels.front(), "id");
    require(query(catalog, Domain::graphics, Read::byId, {id}) == panels.front(),
            "first demand-loaded graphic lookup changed");
    require(loads.size() == 1 && loads.at("graphic") == 1, "graphic lookup loaded another domain");
    const auto *graphic = &catalog.resource("graphic");
    const auto &firstTool = array(fixture.at("tools")).front();
    require(query(catalog, Domain::tools, Read::tool, {get(firstTool, "key")}) == firstTool,
            "demand-loaded tool lookup changed");
    require(loads.size() == 2 && loads.at("tools") == 1, "tool lookup loaded unrelated domains");
    require(query(catalog, Domain::elements, Read::shapes) == get(fixture.at("elements"), "shapes"),
            "demand-loaded shape lookup changed");
    require(&catalog.resource("graphic") == graphic, "loading another domain invalidated resource addresses");
    aliasesAndDetachedReplies(catalog);
    require(loads.size() == 3 && loads.at("graphic") == 1 && loads.at("tools") == 1 && loads.at("elements") == 1,
            "repeated queries reloaded a catalog or loaded unused domains");

    ViewCatalog preloaded({{"tools", fixture.at("tools")}}, [](std::string_view) -> std::optional<Value> {
        throw std::runtime_error("preloaded tool query requested unrelated resources");
    });
    require(query(preloaded, Domain::tools, Read::tool, {get(firstTool, "key")}) == firstTool,
            "preloaded resources did not take precedence over the loader");
    ViewCatalog separate({}, [](std::string_view name) -> std::optional<Value> {
        if (name == "tools")
            return Value(Array{Value(Object{{"key", Value("isolated")}, {"title", Value("Second catalog")}})});
        return std::nullopt;
    });
    require(text(get(query(separate, Domain::tools, Read::tool, {Value("isolated")}), "title")) == "Second catalog",
            "catalog instances shared resource state");
    require(query(separate, Domain::tools, Read::tool, {get(firstTool, "key")}).kind() == ii::core::ValueKind::null,
            "catalog instance reused another instance's index");
}
void nestedRowsAndDefaultPrecedence() {
    const auto parsed = ii::json::parse(R"({"panels":[
      {"id":"p1","name":"First","operation":"paint","rows":[
        {"parameter":"gain","initial":1,"editor":"slider","target":"shared","rows":[
          {"parameter":"gain","initial":2,"editor":"slider","target":"nested"},
          {"parameter":"preview","initial":true,"uiOnly":true}]}
      ],"availableEditors":[
        {"editor":"imageInput","target":"extra"},{"editor":"ignored","target":"shared"}]},
      {"id":"p2","name":"Second","operation":"paint","rows":[
        {"parameter":"gain","initial":3,"editor":"second","target":"shared"},
        {"parameter":"color","sdkParameter":"fill.color","initial":"#aabbcc"}]}
    ],"defaults":{"paint":"p1"},"overviews":[]})");
    require(bool(parsed), "invalid nested graphic fixture");
    const auto graphic = *parsed;
    Value detached;
    {
        ViewCatalog catalog({{"graphic", graphic}});
        const auto &first = array(get(graphic, "panels")).front();
        const auto &parent = array(get(first, "rows")).front();
        const auto &children = array(get(parent, "rows"));
        require(query(catalog, Domain::graphics, Read::flattenedRows, {Value("p1")}) ==
                    Value(Array{parent, children[0], children[1]}), "nested rows changed order or lost children");
        require(query(catalog, Domain::graphics, Read::editorFor, {Value("shared")}) == parent,
                "first matching editor lost precedence");
        require(query(catalog, Domain::graphics, Read::editorFor, {Value("extra")}) ==
                    array(get(first, "availableEditors")).front(), "available editor was not indexed");
        require(query(catalog, Domain::graphics, Read::defaultsFor, {Value("First")}) ==
                    Value(Object{{"gain", Value(2)}}), "panel defaults changed nested last-value precedence");
        require(query(catalog, Domain::graphics, Read::defaultsFor, {Value("paint")}) ==
                    Value(Object{{"gain", Value(2)}}), "default panel alias did not resolve");
        require(query(catalog, Domain::graphics, Read::operationDefaults, {Value("paint")}) ==
                    Value(Object{{"gain", Value(1)}, {"fill.color", Value("#aabbcc")}}),
                "operation defaults changed first-value precedence or included UI state");
        require(array(query(catalog, Domain::graphics, Read::flattenedRows, {Value("missing")})).empty(),
                "missing panel did not return empty rows");
        detached = query(catalog, Domain::graphics, Read::byId, {Value("p1")});
    }
    require(detached == array(get(graphic, "panels")).front(), "reply did not outlive its catalog");
}
}
int main(int argc, char **argv) try {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    const bool measure = mode == "--measure" || mode == "--measure-indexed";
    auto fixture = resources();
    const auto constructed = Clock::now();
    ViewCatalog catalog(std::move(fixture));
    const auto constructionNs = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - constructed).count();
    const Value image(Object{{"imageSource", Value("file:///unchanged.png")}, {"mediaType", Value("Image")}});
    const auto queries = Clock::now();
    std::size_t checksum = 0;
    for (int i = 0; i < 1000; ++i) {
        const auto reply = query(catalog, Domain::facts, Read::source, {image});
        require(text(reply) == "file:///unchanged.png", "metadata source query changed");
        checksum += text(reply).size();
    }
    const auto queryNs = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - queries).count();
    if (mode == "--measure-indexed")
        aliasesAndDetachedReplies(catalog);
    if (measure) {
        std::cout << "catalog_construct_ns=" << constructionNs << " facts_queries_ns=" << queryNs
                  << " query_checksum=" << checksum << '\n';
    } else {
        aliasesAndDetachedReplies(catalog);
        nestedRowsAndDefaultPrecedence();
        demandLoading(resources());
        std::cout << "catalog aliases, nested rows, default precedence, detached replies and demand loading passed\n";
    }
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
