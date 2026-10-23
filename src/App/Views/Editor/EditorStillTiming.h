#pragma once
#include <iiSharedCanvas/Document/Document.h>
#include <algorithm>

namespace dreamscapes {
inline bool staticStill(const iiSharedCanvas::Layer &layer) {
    return iiSharedCanvas::layerTiming(layer)==iiSharedCanvas::LayerTiming::Static
        &&iiSharedCanvas::contentKind(layer)!=iiSharedCanvas::ContentKind::Video;
}
inline iiSharedCanvas::FrameIndex defaultStillFrames(const iiSharedCanvas::Document &document) {
    const auto rate=document.timeline.frameRate;
    if(rate.denominator==0)return 1;
    return iiSharedCanvas::FrameIndex(std::max<std::uint64_t>(1,
        (std::uint64_t(rate.numerator)+rate.denominator-1)/rate.denominator));
}
template<class Eligible>
void assignDefaultStillRanges(iiSharedCanvas::Document &document,const Eligible &eligible) {
    using namespace iiSharedCanvas;
    const auto needsRange=[&](const Layer &layer){return eligible(layer)&&staticStill(layer)&&!layerProperties(layer).frameRange;};
    if(!std::any_of(document.layers.begin(),document.layers.end(),needsRange))return;
    const auto previousFrames=document.timeline.frameCount,frames=std::max(previousFrames,defaultStillFrames(document));
    if(previousFrames==0)return;
    for(auto &layer:document.layers) {
        auto &properties=layerProperties(layer);
        if(needsRange(layer))properties.frameRange=LayerFrameRange{0,frames-1};
        else if(!properties.frameRange&&frames>previousFrames)properties.frameRange=LayerFrameRange{0,previousFrames-1};
    }
    document.timeline.frameCount=frames;
}
inline void normalizeImplicitStill(iiSharedCanvas::Document &document) {
    const bool implicit=document.timeline.frameCount==1&&document.frames.empty()
        &&document.audioTracks.empty()&&!document.layers.empty()
        &&std::all_of(document.layers.begin(),document.layers.end(),[](const auto &layer) {
            const auto &p=iiSharedCanvas::layerProperties(layer);return staticStill(layer)&&!p.frameRange&&p.motion.empty();
        });
    if(implicit)assignDefaultStillRanges(document,[](const auto &){return true;});
}
}
