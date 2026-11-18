#pragma once
#include <iiSDKCore>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace dreamscapes::native {
struct GenerationWorkerPaths final {
    std::filesystem::path explicitExecutable,sdkRoot,python;
    bool windows=false;
    std::filesystem::path configuredExecutable;
    bool requireConfiguredExecutable=true;
};
// Performs filesystem checks only; call from a worker, not the GUI thread.
[[nodiscard]] ii::core::Result<std::vector<std::string>> findGenerationWorker(const GenerationWorkerPaths &);
struct GenerationRuntimeDiscovery final {
    std::vector<std::string> command;
    std::filesystem::path stagingRoot;
};
// Runs filesystem discovery on the supplied executor and delivers on the
// event loop. An empty stagingRoot uses the platform temporary directory.
[[nodiscard]] ii::core::Result<void> discoverGenerationRuntime(ii::runtime::EventLoop &,
    std::function<bool(ii::runtime::Task)> submit,GenerationWorkerPaths,std::filesystem::path stagingRoot,
    std::function<void(ii::core::Result<GenerationRuntimeDiscovery>)> completion);
}
