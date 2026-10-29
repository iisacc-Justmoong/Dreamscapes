#pragma once
#include "ViewValue.h"
#include <iiSDKCore/Runtime/Query.h>
#include <functional>
#include <map>

namespace dreamscapes::vm {
enum class Domain {
    tools,
    graphics,
    graphicState,
    file,
    elements,
    text,
    photo,
    compact,
    viewOptions,
    selection,
    storage,
    facts
};
enum class Read {
    tool,
    definition,
    views,
    shapes,
    view,
    field,
    defaults,
    runtimeDefinition,
    applicableFields,
    rows,
    catalog,
    normalizedTool,
    panelsFor,
    byId,
    overviewFor,
    defaultsFor,
    operationDefaults,
    controls,
    shapeView,
    roleValues,
    readout,
    fillColor,
    sections,
    source,
    key,
    isImage,
    historyImages,
    kind,
    dimensions,
    structure,
    date,
    state,
    model,
    resultSummary,
    sectionFiles,
    listing,
    fileIndices,
    fileIndex,
    mergedFields,
    stateIndices,
    sectionRowCount,
    containsView,
    fieldsByType,
    flattenedRows,
    editorFor
};
struct ViewRead {
    Read operation;
    std::vector<Value> arguments;
};

// Per-instance, owner-thread query receiver. Catalogs are immutable once loaded;
// loading and indexing occur on demand on that same thread. The optional loader
// supplies values without introducing document, filesystem or inference APIs.
class ViewCatalog final {
  public:
    using Resources = std::map<std::string, Value, std::less<>>;
    using ResourceLoader = std::function<std::optional<Value>(std::string_view)>;

    explicit ViewCatalog(Resources resources, ResourceLoader loader = {});
    ViewCatalog(const ViewCatalog &) = delete;
    ViewCatalog &operator=(const ViewCatalog &) = delete;
    ViewCatalog(ViewCatalog &&) = delete;
    ViewCatalog &operator=(ViewCatalog &&) = delete;
    const ii::runtime::QueryPort<ViewRead, Value> &queries(Domain domain) const;
    const Value &resource(std::string_view name) const;

  private:
    Value read(Domain, const ViewRead &) const;
    using ValueRef = std::reference_wrapper<const Value>;
    using RowRefs = std::vector<ValueRef>;
    void indexTools() const;
    void indexGraphics() const;
    const Value &tool(std::string_view) const;
    const Value &graphic(std::string_view) const;
    Value graphicDefaults(std::string_view, bool operation) const;
    mutable Resources resources_;
    ResourceLoader loader_;
    std::map<Domain, std::unique_ptr<ii::runtime::QueryPort<ViewRead, Value>>> queries_;
    mutable bool toolsIndexed_ = false, graphicsIndexed_ = false;
    // References stay internal: resource map insertions preserve addresses, and
    // query replies still own their values independently of the catalog.
    mutable std::map<std::string, ValueRef, std::less<>> graphicById_, toolById_, graphicEditors_;
    mutable std::map<std::string, RowRefs, std::less<>> graphicRows_;
    mutable std::map<std::string, Object, std::less<>> operationDefaults_;
};
Value imageSource(const Value &entry);
Value imageKey(const Value &entry);
bool isImage(const Value &entry);
Value historyImages(const Value &entries);
Value sectionFiles(const Value &aliases, std::string_view section);
Value storageListing(const Value &files, std::string_view section, std::string_view directory, std::string query,
                     std::string_view sort);
} // namespace dreamscapes::vm
