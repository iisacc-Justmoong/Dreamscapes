#pragma once

#include "ViewValue.h"
#include <iiSDKCore/Runtime/Query.h>

namespace dreamscapes::vm {
// Pure presentation queries. Inputs are detached values; no query can reach a
// document, storage handle, inference engine, QObject or UI item.
enum class ProjectionRead { labels, values, choices, index, indices, matchingIndices, concatenateIndices,
                            join, counts, notifications, timelineLayout, timecode, trackLabels,
                            videoResults, sizeChoices, nextChoice, filenames, rulerStep, fileField };
struct ProjectionQuery final {
    ProjectionRead operation;
    std::vector<Value> arguments;
};
class ViewProjection final {
  public:
    ViewProjection();
    const ii::runtime::QueryPort<ProjectionQuery, Value> &queries() const { return queries_; }
  private:
    static Value read(const ProjectionQuery &);
    ii::runtime::QueryPort<ProjectionQuery, Value> queries_;
};
} // namespace dreamscapes::vm
