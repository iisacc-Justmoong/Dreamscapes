#pragma once
#include "ViewValue.h"
#include <iiSDKCore/Runtime/Query.h>

namespace dreamscapes::vm {
struct GenerationSnapshot {
    Value jobs, results, currentResult, requestIds, inferenceStatus;
    bool connected = true, busy = false;
    int previewStep = 0, previewTotalSteps = 0, elapsedSeconds = 0;
    std::string selectedResultId;
};
bool generationPending(std::string_view state);
std::string generationStateLabel(std::string_view);
Value generationPresentation(const GenerationSnapshot &);
class GenerationPresenter final {
public:
    GenerationPresenter() : queries_([](const GenerationSnapshot &s) { return generationPresentation(s); }) {}
    const ii::runtime::QueryPort<GenerationSnapshot, Value> &queries() const { return queries_; }
private:
    ii::runtime::QueryPort<GenerationSnapshot, Value> queries_;
};
} // namespace dreamscapes::vm
