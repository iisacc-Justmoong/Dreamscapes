#pragma once
#include "App/Generation/NativeWorkerProtocol.h"
#include <iiFileProvider>
#include <filesystem>
#include <memory>
#include <vector>

namespace dreamscapes::native::detail {
// Private worker output is removed only after successful publication or when
// it is known to contain no output. This object never owns public files.
struct GenerationStaging final : generation::WorkerStaging {
    explicit GenerationStaging(const std::filesystem::path &root);
    ~GenerationStaging() override;
    void workerFinished(generation::QueueCompletion &) noexcept override;
    iiFileProvider::native::TemporaryDirectory directory;
    std::filesystem::path path;
    vm::Value context;
    // Retain private input snapshots until the worker no longer needs them.
    std::vector<std::shared_ptr<void>> inputs;
    bool preserve=false;
};
}
