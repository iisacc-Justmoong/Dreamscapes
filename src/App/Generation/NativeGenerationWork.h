#pragma once
#include <Generation/NativeDiffusion.hpp>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>

namespace dreamscapes {
using NativeGenerationOperation = std::function<iiLocalDiffusion::NativeGenerationResult(
    const iiLocalDiffusion::NativeGenerationRequest &, const iiLocalDiffusion::NativeGenerationOptions &,
    const std::atomic_bool &, const iiLocalDiffusion::NativeProgressCallback &,
    const iiLocalDiffusion::NativePreviewCallback &)>;
struct NativeGenerationState {
    std::atomic_bool cancelled{false}, accepting{true};
    std::mutex previewMutex;
    int previewSequence = 0;
};
struct NativePreviewArtifact {
    std::string name;
    int sequence = 0, step = 0, total = 0;
};
struct NativeGenerationInput {
    iiLocalDiffusion::NativeGenerationRequest request;
    iiLocalDiffusion::NativeGenerationOptions options;
    NativeGenerationOperation generate;
    std::shared_ptr<NativeGenerationState> state;
    std::filesystem::path previewDirectory;
    std::shared_ptr<void> lifetime;
    iiLocalDiffusion::NativeProgressCallback progress;
    std::function<void(NativePreviewArtifact)> preview;
};
// Worker-only inference and PNG preview preparation. Values and a source lease
// outlive the presenter; callbacks may post values but never borrow an owner.
iiLocalDiffusion::NativeGenerationResult runNativeGeneration(NativeGenerationInput input);
}
