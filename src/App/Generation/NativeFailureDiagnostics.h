#pragma once

#include "NativeGenerationQueue.h"

namespace dreamscapes::generation {
// Projection only: does not execute a model, retry work, or mutate the job.
[[nodiscard]] vm::Value nativeFailureReport(const QueueJob &job);
}
