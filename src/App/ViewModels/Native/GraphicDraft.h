#pragma once
#include "EditorToolState.h"

namespace dreamscapes::vm {
enum class GraphicAction { restoreView, selectView, selectCollection, addCollectionItem, removeCollectionItem, edit, resetView, applyReturnedParameters, setIndices };
struct GraphicCommand { GraphicAction action; std::vector<Value> arguments; };
class GraphicDraft final {
public:
    using ToolRead = std::function<Value()>;
    using ToolWrite = std::function<void(const ToolCommand &)>;
    GraphicDraft(ViewCatalog &, ToolRead, ToolWrite);
    const ii::runtime::QueryPort<GraphicCommand, bool> &commands() const { return commands_; }
    std::string defaultView() const;
    std::string currentViewId() const { return viewId_; }
    Value selectedView() const;
    Value viewCatalog() const;
    Value indices() const { return indices_; }
    std::string operationFor(const Value &row) const;
    Value rowsOf(const Value &) const;
    Value parameters(std::string_view operation) const;
    std::string parameterPath(const Value &) const;
    std::optional<Value> value(const Value &) const;
    Value collectionDefinition() const;
    Value collectionItems() const;
    Value collectionItem() const;
    Value curvePoints(const Value &) const;
    Value statusFor(std::string_view) const;
private:
    bool execute(const GraphicCommand &);
    Value read(Read, std::vector<Value>) const;
    Value toolValues() const;
    bool selectView(std::string_view);
    void selectCollection(int);
    ViewCatalog &catalog_;
    ToolRead toolRead_;
    ToolWrite toolWrite_;
    std::string viewId_, indicesOperation_;
    Value indices_{Object{}};
    Object indicesByOperation_;
    ii::runtime::QueryPort<GraphicCommand, bool> commands_;
};
} // namespace dreamscapes::vm
