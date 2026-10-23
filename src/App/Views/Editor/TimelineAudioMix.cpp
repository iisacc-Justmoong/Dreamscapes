#include "TimelineAudioMix.h"
#include <array>
#include <algorithm>
#include <cmath>
iiSharedCanvas::AudioImportResult dreamscapes::mixTimelineAudio(const iiSharedCanvas::Document &document, iiSharedCanvas::FrameIndex firstFrame,std::stop_token cancelled) {
    using namespace iiSharedCanvas;
    const auto aborted=[] {return AudioImportResult{{},{MediaIoCode::Cancelled,"Audio preparation cancelled."}};};
    if(cancelled.stop_requested())return aborted();
    if (firstFrame >= document.timeline.frameCount) return {{}, {MediaIoCode::InvalidArgument, "Choose a frame inside the audio timeline."}};
    constexpr std::uint32_t rate = 48000;
    const auto count = audioSampleFrameCount(document.timeline.frameCount - firstFrame, document.timeline.frameRate, rate);
    if (!count || *count > 16777216) return {{}, {MediaIoCode::LimitExceeded, "The playback mix exceeds its PCM allocation limit."}};
    const double fps = double(document.timeline.frameRate.numerator) / document.timeline.frameRate.denominator;
    AudioAsset result{"editor.timeline.playback", rate, 2, std::vector<std::int16_t>(*count * 2)};
    struct Span {
        const AudioAsset *source;
        std::int64_t start, end;
        double clipStart, gain;
        std::uint64_t sourceOffset;
    };
    std::vector<Span> spans;
    for (const auto &track : document.audioTracks) if (!track.muted) for (const auto &clip : track.clips) if (clip.enabled) {
        if(cancelled.stop_requested())return aborted();
        if(!std::isfinite(track.gainDb)||!std::isfinite(clip.gainDb))
            return {{},{MediaIoCode::InvalidArgument,"Choose finite audio gains."}};
        const auto *source = findAudioAsset(document, clip.assetId);
        if (!source || source->sampleRate == 0 || source->channelCount < 1 || source->channelCount > 2 || source->samples.empty()) continue;
        const double clipStart = clip.startFrame / fps, clipEnd = (std::uint64_t(clip.startFrame) + clip.durationFrames) / fps;
        const auto start = std::max<std::int64_t>(0, std::int64_t(std::ceil((clipStart - firstFrame / fps) * rate)));
        const auto end = std::min<std::int64_t>(*count, std::int64_t(std::ceil((clipEnd - firstFrame / fps) * rate)));
        const double gain = std::pow(10.0, (track.gainDb + clip.gainDb) / 20);
        if (start < end) spans.push_back({source, start, end, clipStart, gain, clip.sourceOffsetSamples});
    }
    // Accumulate in the original track/clip order, using bounded scratch storage.
    // Keep rounding and clipping until after every overlapping clip is mixed.
    constexpr std::int64_t blockFrames = 8192;
    std::array<double, blockFrames * 2> mixed;
    for (std::int64_t block = 0; block < std::int64_t(*count); block += blockFrames) {
        if(cancelled.stop_requested())return aborted();
        const auto blockEnd = std::min<std::int64_t>(*count, block + blockFrames);
        std::fill(mixed.begin(), mixed.begin() + (blockEnd - block) * 2, 0.0);
        for (const auto &span : spans) {
            const auto *source = span.source;
            const auto sourceCount = source->samples.size() / source->channelCount;
            for (auto i = std::max(block, span.start); i < std::min(blockEnd, span.end); ++i) {
                const double position = ((firstFrame / fps + double(i) / rate) - span.clipStart) * source->sampleRate + span.sourceOffset;
                if (position < 0 || position >= sourceCount) continue;
                const auto left = std::size_t(position), right = std::min(left + 1, sourceCount - 1); const double fraction = position - left;
                for (int channel = 0; channel < 2; ++channel) {
                    const auto c = std::min<int>(channel, source->channelCount - 1);
                    const double sample = source->samples[left * source->channelCount + c] * (1 - fraction) + source->samples[right * source->channelCount + c] * fraction;
                    mixed[(i - block) * 2 + channel] += sample * span.gain;
                }
            }
        }
        for (std::int64_t i = 0; i < (blockEnd - block) * 2; ++i)
            result.samples[block * 2 + i] = std::int16_t(std::clamp(std::lround(mixed[i]), -32768L, 32767L));
    }
    return {std::move(result), {}};
}
