#include "NativeGenerationWork.h"
#include <iiFileProviderNative.h>
#include <iiImageCodec/ImageCodec.h>
#include <exception>

namespace dreamscapes {
iiLocalDiffusion::NativeGenerationResult runNativeGeneration(NativeGenerationInput input)
{
    iiLocalDiffusion::NativeGenerationResult result;
    if (!input.state || !input.generate) { result.error = "The native image operation is unavailable."; return result; }
    const auto state = input.state;
    struct Retire { std::shared_ptr<NativeGenerationState> state; ~Retire() { state->accepting = false; } } retire{state};
    if (state->cancelled || !state->accepting) { result.cancelled = true; return result; }
    const auto interested = [state] { return state->accepting.load() && !state->cancelled.load(); };
    const auto progress = [interested, receive = std::move(input.progress)](const auto &event) {
        if (interested() && receive) receive(event);
    };
    const std::weak_ptr<void> source = input.lifetime;
    const auto preview = [state, interested, source, directory = input.previewDirectory,
                          receive = std::move(input.preview)](const iiLocalDiffusion::NativeGenerationPreview &frame) {
        if (!interested() || !receive || directory.empty() || frame.sequence < 1
            || frame.width < 1 || frame.height < 1 || frame.width > 512 || frame.height > 512
            || frame.step < 1 || frame.total < frame.step || frame.total > 10000
            || frame.rgb.size() != std::size_t(frame.width) * frame.height * 3) return;
        const auto lease = source.lock(); if (!lease) return;
        std::unique_lock lock(state->previewMutex);
        if (!interested() || frame.sequence <= state->previewSequence) return;
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error) || error
            || std::filesystem::is_symlink(directory, error) || error) return;
        const auto canonical = std::filesystem::canonical(directory, error);
        if (error || canonical != directory) return;
        auto digits = std::to_string(frame.sequence);
        const auto name = "step-" + std::string(digits.size() < 6 ? 6 - digits.size() : 0, '0') + digits + ".png";
        const auto png = ii::image::encode_png({static_cast<std::uint32_t>(frame.width),
            static_cast<std::uint32_t>(frame.height), ii::image::PixelFormat::rgb8, 0, frame.rgb});
        if (!png || !interested()) return;
        try {
            iiFileProvider::native::File::write(directory / name,
                std::string_view(reinterpret_cast<const char *>(png->data()), png->size()));
        } catch (const std::exception &) { return; }
        if (!interested()) return;
        state->previewSequence = frame.sequence;
        lock.unlock();
        // A receiver can initiate another operation; do not invoke it under
        // the encoder's serialization lock.
        if (!interested()) return;
        receive({name, frame.sequence, frame.step, frame.total});
    };
    try { result = input.generate(input.request, input.options, state->cancelled, progress, preview); }
    catch (const std::exception &error) { result.error = error.what(); }
    catch (...) { result.error = "The native image engine stopped unexpectedly."; }
    return result;
}
}
