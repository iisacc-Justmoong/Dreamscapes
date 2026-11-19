#pragma once
#include "App/Generation/NativeWorkerProtocol.h"
#include "NativeSocietyStorage.h"
#include <filesystem>

namespace dreamscapes::native {
struct SocietyGenerationOptions final {
    std::filesystem::path selection,explicitRoot,stagingRoot;
    SocietyStorageAccess::Read readStorage;
    /// Uses the injected daemon on desktop and the local provider for existing
    /// explicitly supplied on-device selections.
    [[nodiscard]] SocietyStorageAccess::Read storageReader() const {
        return readStorage?readStorage:localSocietyStorage(selection,explicitRoot).read;
    }
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
