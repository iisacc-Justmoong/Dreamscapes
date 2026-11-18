#include "NativeAudioTrack.h"
#include <Document/DocumentEditor.h>
#include <Media/MediaIo.h>
#include <Validation/Validation.h>
#include <iiSDKCore>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dreamscapes::editor {
namespace {
using namespace iiSharedCanvas;using ii::core::ErrorCode;
template<class Predicate> std::string unique(std::string_view prefix,Predicate exists) {
    for (std::uint64_t n=1;n<std::numeric_limits<std::uint64_t>::max();++n) {
        auto id=std::string(prefix)+std::to_string(n);if (!exists(id)) return id;
    }
    throw std::overflow_error("Audio identity is exhausted");
}
ii::core::Result<void> verify(const Document &doc) {
    const auto result=validate(doc);
    if (!result.ok()) return ii::core::fail(ErrorCode::invalid_argument,result.issues.front().message);
    return {};
}
}
ii::core::Result<PreparedAudioEdit> prepareAudioTrackCreate(std::shared_ptr<const Document> source,
    std::uint64_t generation,std::uint64_t revision,std::function<bool()> cancelled) {
    if (!source) return ii::core::fail(ErrorCode::closed,"Open a canvas first");
    try {
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
        auto candidate=std::make_shared<Document>(*source);
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
        const auto id=unique("audio.track.",[&](const auto &value){return findLayer(*candidate,value) || findAudioTrack(*candidate,value);});
        const auto name=unique("Audio ",[&](const auto &value){return std::ranges::any_of(candidate->audioTracks,
            [&](const auto &track){return track.name==value;}) || std::ranges::any_of(candidate->layers,
            [&](const auto &layer){return layerProperties(layer).name==value;});});
        DocumentEditor editor(*candidate);const auto inserted=editor.insertAudioTrack(AudioTrackLayer{.id=id,.name=name});
        if (!inserted.ok()) return ii::core::fail(ErrorCode::invalid_argument,inserted.message);
        if (auto checked=verify(*candidate);!checked) return std::unexpected(checked.error());
        return PreparedAudioEdit{std::move(source),std::move(candidate),generation,revision,true,id};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot prepare audio track",std::current_exception());}
}
ii::core::Result<PreparedAudioEdit> prepareAudioImport(std::shared_ptr<const Document> source,
    std::uint64_t generation,std::uint64_t revision,std::string_view trackId,std::int64_t first,
    AudioAsset asset,std::string name,std::function<bool()> cancelled) {
    if (!source) return ii::core::fail(ErrorCode::closed,"Open a canvas first");
    if (trackId.empty() || !findAudioTrack(*source,std::string(trackId)))
        return ii::core::fail(ErrorCode::invalid_argument,"Select an audio track first");
    if (first<0 || std::uint64_t(first)>=source->timeline.frameCount || name.empty() || name.size()>4096 ||
        !ii::core::valid_utf8(name) || name.find('\0')!=std::string::npos)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid audio placement or name");
    const MediaLimits limits;
    if (asset.sampleRate<8000 || asset.sampleRate>192000 || asset.channelCount<1 || asset.channelCount>2 ||
        asset.samples.empty() || asset.samples.size()%asset.channelCount!=0 ||
        asset.samples.size()>limits.maxDecodedBytes/sizeof(std::int16_t))
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid PCM16 audio source");
    const auto fps=source->timeline.frameRate;
    if (!fps.numerator || !fps.denominator) return ii::core::fail(ErrorCode::invalid_argument,"Invalid canvas frame rate");
    const auto samples=std::uint64_t(asset.samples.size()/asset.channelCount);
    constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
    if (samples>maximum/fps.numerator || std::uint64_t(asset.sampleRate)>maximum/fps.denominator)
        return ii::core::fail(ErrorCode::out_of_range,"Audio duration exceeds the timeline limit");
    const auto numerator=samples*fps.numerator,denominator=std::uint64_t(asset.sampleRate)*fps.denominator;
    const auto duration=numerator/denominator+(numerator%denominator!=0);
    if (!duration || duration>std::numeric_limits<FrameIndex>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Audio duration exceeds the timeline limit");
    const auto required=audioSampleFrameCount(static_cast<FrameIndex>(duration),fps,asset.sampleRate);
    if (!required || *required<samples || *required>limits.maxDecodedBytes/(sizeof(std::int16_t)*asset.channelCount) ||
        std::uint64_t(first)+duration>std::numeric_limits<FrameIndex>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Audio exceeds the supported timeline or allocation limit");
    const auto end=std::uint64_t(first)+duration;
    const auto *track=findAudioTrack(*source,std::string(trackId));
    if (std::ranges::any_of(track->clips,[&](const auto &clip){return std::uint64_t(clip.startFrame)<end &&
        std::uint64_t(clip.startFrame)+clip.durationFrames>std::uint64_t(first);}))
        return ii::core::fail(ErrorCode::invalid_argument,"The audio clip overlaps an existing clip on this track");
    try {
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
        asset.samples.resize(static_cast<std::size_t>(*required)*asset.channelCount,0);
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
        auto candidate=std::make_shared<Document>(*source);
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
        asset.id=unique("audio.asset.",[&](const auto &id){return findAsset(*candidate,id) || findAudioAsset(*candidate,id);});
        const auto clipId=unique("audio.clip.",[&](const auto &id){
            return std::ranges::any_of(candidate->audioTracks,[&](const auto &item){
                return std::ranges::any_of(item.clips,[&](const auto &clip){return clip.id==id;});});
        });
        DocumentEditor editor(*candidate);
        if (end>candidate->timeline.frameCount) {
            const auto resized=editor.setFrameCount(static_cast<FrameIndex>(end));
            if (!resized.ok()) return ii::core::fail(ErrorCode::invalid_argument,resized.message);
        }
        const auto id=asset.id;
        auto inserted=editor.insertAudioAsset(std::move(asset));
        if (!inserted.ok()) return ii::core::fail(ErrorCode::invalid_argument,inserted.message);
        inserted=editor.insertAudioClip(std::string(trackId),AudioClip{clipId,std::move(name),id,
            static_cast<FrameIndex>(first),static_cast<FrameIndex>(duration)});
        if (!inserted.ok()) return ii::core::fail(ErrorCode::invalid_argument,inserted.message);
        if (auto checked=verify(*candidate);!checked) return std::unexpected(checked.error());
        return PreparedAudioEdit{std::move(source),std::move(candidate),generation,revision,true};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot prepare audio import",std::current_exception());}
}
ii::core::Result<PreparedAudioEdit> prepareAudioTrackPatch(std::shared_ptr<const Document> source,
    std::uint64_t generation,std::uint64_t revision,std::string_view trackId,std::optional<bool> muted,std::optional<double> gain,std::function<bool()> cancelled) {
    if (!source) return ii::core::fail(ErrorCode::closed,"Open a canvas first");
    const auto *track=findAudioTrack(*source,std::string(trackId));
    if (!track) return ii::core::fail(ErrorCode::invalid_argument,"Select an audio track first");
    if (gain && (!std::isfinite(*gain) || *gain < -96 || *gain > 24))
        return ii::core::fail(ErrorCode::out_of_range,"Audio gain must be between -96 and 24 dB");
    if ((!muted || *muted==track->muted) && (!gain || *gain==track->gainDb))
        return PreparedAudioEdit{std::move(source),{},generation,revision,false};
    try {
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
        auto replacement=*track;if (muted) replacement.muted=*muted;if (gain) replacement.gainDb=*gain;
        auto candidate=std::make_shared<Document>(*source);DocumentEditor editor(*candidate);
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
        const auto result=editor.replaceAudioTrack(std::string(trackId),std::move(replacement));
        if (!result.ok()) return ii::core::fail(ErrorCode::invalid_argument,result.message);
        if (auto checked=verify(*candidate);!checked) return std::unexpected(checked.error());
        return PreparedAudioEdit{std::move(source),std::move(candidate),generation,revision,true};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot prepare audio track edit",std::current_exception());}
}
ii::core::Result<PreparedAudioEdit> prepareAudioClipTrim(std::shared_ptr<const Document> source,
    std::uint64_t generation,std::uint64_t revision,std::string_view trackId,std::string_view clipId,
    std::int64_t first,std::int64_t end,std::function<bool()> cancelled) {
    if (!source) return ii::core::fail(ErrorCode::closed,"Open a canvas first");
    const auto *track=findAudioTrack(*source,std::string(trackId));
    const auto *clip=track?findAudioClip(*track,std::string(clipId)):nullptr;
    const auto *asset=clip?findAudioAsset(*source,clip->assetId):nullptr;
    if (!clip || !asset) return ii::core::fail(ErrorCode::invalid_argument,"Select an existing audio clip");
    const auto oldFirst=std::int64_t(clip->startFrame),oldEnd=oldFirst+clip->durationFrames;
    if (first<oldFirst || end>oldEnd || first>=end)
        return ii::core::fail(ErrorCode::out_of_range,"Trim range must lie inside the selected clip");
    if (first==oldFirst && end==oldEnd) return PreparedAudioEdit{std::move(source),{},generation,revision,false};
    const auto fps=source->timeline.frameRate;
    if (!fps.numerator || !fps.denominator || !asset->sampleRate)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid audio timing");
    const auto shift=std::uint64_t(first-oldFirst);
    constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
    if (shift>maximum/fps.denominator || shift*fps.denominator>maximum/asset->sampleRate)
        return ii::core::fail(ErrorCode::out_of_range,"Audio trim offset exceeds the supported range");
    const auto sampleShift=shift*fps.denominator*asset->sampleRate/fps.numerator;
    if (sampleShift>maximum-clip->sourceOffsetSamples)
        return ii::core::fail(ErrorCode::out_of_range,"Audio trim offset exceeds the supported range");
    try {
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
        auto replacement=*clip;replacement.startFrame=static_cast<FrameIndex>(first);
        replacement.durationFrames=static_cast<FrameIndex>(end-first);replacement.sourceOffsetSamples+=sampleShift;
        auto candidate=std::make_shared<Document>(*source);
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
        DocumentEditor editor(*candidate);
        const auto result=editor.replaceAudioClip(std::string(trackId),std::string(clipId),std::move(replacement));
        if (!result.ok()) return ii::core::fail(ErrorCode::invalid_argument,result.message);
        if (auto checked=verify(*candidate);!checked) return std::unexpected(checked.error());
        return PreparedAudioEdit{std::move(source),std::move(candidate),generation,revision,true};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot prepare audio trim",std::current_exception());}
}
ii::core::Result<PreparedAudioEdit> prepareAudioClipMove(std::shared_ptr<const Document> source,
    std::uint64_t generation,std::uint64_t revision,std::string_view trackId,std::string_view clipId,
    std::int64_t first,std::function<bool()> cancelled) {
    if (!source) return ii::core::fail(ErrorCode::closed,"Open a canvas first");
    const auto *track=findAudioTrack(*source,std::string(trackId));
    const auto *clip=track?findAudioClip(*track,std::string(clipId)):nullptr;
    if (!clip) return ii::core::fail(ErrorCode::invalid_argument,"Select an existing audio clip");
    if (first<0 || std::uint64_t(first)+clip->durationFrames>source->timeline.frameCount)
        return ii::core::fail(ErrorCode::out_of_range,"Audio clip is outside the timeline");
    const auto end=std::uint64_t(first)+clip->durationFrames;
    if (std::ranges::any_of(track->clips,[&](const auto &other) {return other.id!=clipId &&
        std::uint64_t(other.startFrame)<end && std::uint64_t(other.startFrame)+other.durationFrames>std::uint64_t(first);}))
        return ii::core::fail(ErrorCode::invalid_argument,"Audio clips cannot overlap on a track");
    if (first==clip->startFrame) return PreparedAudioEdit{std::move(source),{},generation,revision,false};
    try {
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
        auto replacement=*clip;replacement.startFrame=static_cast<FrameIndex>(first);
        auto candidate=std::make_shared<Document>(*source);
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
        DocumentEditor editor(*candidate);
        const auto result=editor.replaceAudioClip(std::string(trackId),std::string(clipId),std::move(replacement));
        if (!result.ok()) return ii::core::fail(ErrorCode::invalid_argument,result.message);
        if (auto checked=verify(*candidate);!checked) return std::unexpected(checked.error());
        return PreparedAudioEdit{std::move(source),std::move(candidate),generation,revision,true};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot prepare audio move",std::current_exception());}
}
}
