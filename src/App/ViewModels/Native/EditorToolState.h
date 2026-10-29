#pragma once
#include "ViewCatalog.h"

namespace dreamscapes::vm {
enum class ToolAction {
    write,
    setValue,
    setGraphicValue,
    setGraphicValues,
    replaceGraphicCollection,
    resetGraphicView,
    resetTool,
    acceptedSelection,
    selectElementView,
    editElement,
    resetElementView,
    commitElementNumber,
    selectCompactView,
    resetCompactView,
    editText,
    patchText,
    resetTextView,
    textRole,
    textLinePreset,
    restoreSettings,
    prepareLayerAdjustment,
    editLayerAdjustment,
    expandLayerGroup
};
struct ToolCommand {
    ToolAction action;
    std::vector<Value> arguments;
};
struct ToolEdit {
    std::string tool, field;
    Value value;
};
struct ToolReply {
    std::vector<ToolEdit> edits;
    bool reset = false;
};
class EditorToolState final {
  public:
    explicit EditorToolState(ViewCatalog &);
    const ii::runtime::QueryPort<ToolCommand, ToolReply> &commands() const {
        return commands_;
    }
    Value presentation() const;
    Value inputSnapshot() const;
    Value definition() const;
    Value values() const;
    Value elementValues() const;
    Value textValues() const;
    const Value &collapsedLayerGroups() const { return data_.at("collapsedLayerGroups"); }
    std::string formattedElement(std::string_view field, int index) const;
    bool elementFieldEnabled(std::string_view field) const;
    bool textOptionAvailable(std::string_view field, std::string_view option) const;

  private:
    Value read(Domain, Read, std::vector<Value>) const;
    ToolReply execute(const ToolCommand &);
    ToolReply publishGraphic(std::string_view operation, const Value &draft);
    ViewCatalog &catalog_;
    Object data_;
    Value accepted_;
    ii::runtime::QueryPort<ToolCommand, ToolReply> commands_;
};
} // namespace dreamscapes::vm
