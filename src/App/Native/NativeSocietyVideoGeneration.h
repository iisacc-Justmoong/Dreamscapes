#pragma once
#include "NativeSocietyGeneration.h"

namespace dreamscapes::native {
// Prepares one video from the captured batch request on a worker.
[[nodiscard]] ii::core::Result<generation::WorkerJobPlan> prepareSocietyVideoGeneration(
    const generation::QueueRequest &,const generation::QueueCancellation &,const SocietyGenerationOptions &,
    std::size_t outputIndex=0);
[[nodiscard]] ii::core::Result<void> publishSocietyVideoGeneration(const generation::WorkerJobPlan &,
    const generation::QueueCancellation &,const SocietyGenerationOptions &,const generation::QueueOutputCallback &);
// Executes batch items serially and publishes each completed video independently.
[[nodiscard]] generation::QueueRunner makeSocietyVideoRunner(SocietyGenerationOptions,generation::WorkerTransport);
}
