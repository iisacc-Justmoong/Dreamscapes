#pragma once
#include <iiSharedCanvas/Audio/AudioCodec.h>
#include <stop_token>

namespace dreamscapes {
// Native preview policy: 48 kHz stereo, original clip order/gains/interpolation
// and bounded scratch storage. No GUI, file, device or application owner.
iiSharedCanvas::AudioImportResult mixTimelineAudio(const iiSharedCanvas::Document &,
    iiSharedCanvas::FrameIndex firstFrame,std::stop_token cancelled={});
}
