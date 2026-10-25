#pragma once
#include "ViewValue.h"
#include <iiSDKCore/Runtime/Query.h>
#include <map>

namespace dreamscapes::vm {
enum class Domain { tools, graphics, graphicState, file, elements, text, photo, compact, viewOptions, selection, storage, facts };
enum class Read {
    tool, definition, views, view, field, defaults, runtimeDefinition, applicableFields, rows,
    catalog, normalizedTool, panelsFor, byId, overviewFor, defaultsFor, operationDefaults,
    controls, shapeView, roleValues, readout, fillColor, sections, source, key, isImage,
    historyImages, kind, dimensions, structure, date, state, model, resultSummary,
    sectionFiles, listing
};
struct ViewRead { Read operation; std::vector<Value> arguments; };

// Per-instance, owner-thread query receiver. It owns immutable catalogs and
// computed defaults, and has no document, filesystem or inference dependency.
class ViewCatalog final {
public:
    explicit ViewCatalog(std::map<std::string, Value, std::less<>> resources);
    const ii::runtime::QueryPort<ViewRead, Value> &queries(Domain domain) const;
    const Value &resource(std::string_view name) const;
private:
    Value read(Domain, const ViewRead &) const;
    Value tool(std::string_view) const;
    Value graphic(std::string_view) const;
    Value graphicDefaults(std::string_view, bool operation) const;
    std::map<std::string, Value, std::less<>> resources_;
    std::map<Domain, std::unique_ptr<ii::runtime::QueryPort<ViewRead, Value>>> queries_;
    std::map<std::string, Value, std::less<>> graphicById_, toolById_, operationDefaults_;
};
Value imageSource(const Value &entry);
Value imageKey(const Value &entry);
bool isImage(const Value &entry);
Value historyImages(const Value &entries);
Value sectionFiles(const Value &aliases, std::string_view section);
Value storageListing(const Value &files, std::string_view section, std::string_view directory, std::string query, std::string_view sort);
} // namespace dreamscapes::vm
