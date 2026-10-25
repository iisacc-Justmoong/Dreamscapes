#pragma once
#include "ViewValue.h"
#include <iiSDKCore/Runtime/Query.h>

namespace dreamscapes::vm {
struct WorkspaceChange { Object patch; };
// Navigation and pending open intents are session data. This receiver cannot
// reach a document or a storage provider; only explicit host commands can.
class WorkspaceState final {
public:
    WorkspaceState() : changes_([this](const WorkspaceChange &change) {
        for (const auto &[key, value] : change.patch) if (data_.contains(key)) data_[key] = value;
        return Value(data_);
    }) {
        data_ = {{"resultVisible", Value(false)}, {"editorVisible", Value(false)}, {"publishVisible", Value(false)}, {"storageBrowserSection", Value("")},
            {"pendingFileKey", Value("")}, {"pendingFileSection", Value("")}, {"pendingFileContainerId", Value("")}, {"pendingCanvasOpen", Value()},
            {"canvasImageSource", Value("")}, {"canvasSpecification", Value(Object{})}, {"canvasGenerationResult", Value(Object{})}, {"canvasGenerationResults", Value(Array{})},
            {"currentResult", Value(Object{})}, {"resultJobIds", Value(Array{})}, {"generationRequestError", Value("")}};
    }
    const ii::runtime::QueryPort<WorkspaceChange, Value> &changes() const { return changes_; }
    Value presentation() const { return Value(data_); }
private:
    Object data_;
    ii::runtime::QueryPort<WorkspaceChange, Value> changes_;
};
} // namespace dreamscapes::vm
