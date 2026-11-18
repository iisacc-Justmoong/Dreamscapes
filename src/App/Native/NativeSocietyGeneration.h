#pragma once
#include "App/Generation/NativeWorkerProtocol.h"
#include <filesystem>

namespace dreamscapes::native {
struct SocietyGenerationOptions final {
    std::filesystem::path selection,explicitRoot,stagingRoot;
};
// Runs on a worker. The returned plan owns its private staging directory.
[[nodiscard]] ii::core::Result<generation::WorkerJobPlan> prepareSocietyGeneration(
    const generation::QueueRequest &,const generation::QueueCancellation &,const SocietyGenerationOptions &);
// Validates the worker's committed output and publishes each image exclusively.
[[nodiscard]] ii::core::Result<void> publishSocietyGeneration(const generation::WorkerJobPlan &,
    const generation::QueueCancellation &,const SocietyGenerationOptions &,const generation::QueueOutputCallback &);
[[nodiscard]] generation::WorkerGenerationServices societyGenerationServices(SocietyGenerationOptions,
    generation::WorkerTransport);
}
