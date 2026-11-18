#include "NativeTimelinePanel.h"
#include <LVUI>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dreamscapes::editor {
namespace {
using ii::core::ErrorCode;
std::string displayName(std::string value,std::string_view fallback="Layer") {
    if (value.empty() || !ii::gui::DefaultButtonPolicy::valid_text(value)) return std::string(fallback);
    if (value.size()>4096) {
        value.resize(4096);
        while (!ii::gui::DefaultButtonPolicy::valid_text(value) && !value.empty()) value.pop_back();
    }
    return value;
}
bool editableRange(const iiSharedCanvas::Layer &layer) {
    return iiSharedCanvas::layerKind(layer).has_value();
}
}
TimelinePanelModel::TimelinePanelModel(std::shared_ptr<native::DocumentSession> document,double rowHeight)
    : document_(std::move(document)),rowHeight_(rowHeight) {
    if (!document_ || !std::isfinite(rowHeight_) || rowHeight_<=0 || rowHeight_>8192)
        throw std::invalid_argument("Timeline requires a document and valid row height");
    ii::gui::EditorTimelineOptions value;
    value.palette=lvui::native::editor_timeline_palette<ii::gui::EditorTimelinePalette>();
    value.width=1; value.height=1; value.enabled=false;
    (void)options.set(value);
}
std::shared_ptr<TimelinePanelModel> TimelinePanelModel::create(std::shared_ptr<native::DocumentSession> document,double height) {
    auto value=std::shared_ptr<TimelinePanelModel>(new TimelinePanelModel(std::move(document),height));
    value->start(); return value;
}
void TimelinePanelModel::start() {
    const std::weak_ptr<TimelinePanelModel> weak=shared_from_this();
    frameObserver_=document_->frame.observe([weak](const auto &,std::uint64_t){if (auto self=weak.lock()) self->synchronize(false);});
    auto projection=[weak](const auto &,std::uint64_t){if (auto self=weak.lock()) self->synchronize(true);};
    revisionObserver_=document_->revision.observe(projection); generationObserver_=document_->documentGeneration.observe(projection);
    selectionObserver_=document_->selectedLayer.observe(projection); synchronize(true);
}
void TimelinePanelModel::setAudioClipEdit(AudioClipEdit edit) {
    if (owner_!=std::this_thread::get_id()) return;
    audioClipEdit_=std::move(edit);
    if (gesture_ && gesture_->audio) gesture_.reset();
    synchronize(true);
}
void TimelinePanelModel::synchronize(bool projectionChanged) {
    if (owner_!=std::this_thread::get_id()) return;
    if (synchronizing_) {pending_=true;pendingProjection_|=projectionChanged;return;}
    synchronizing_=true;
    try {
        do {
            pending_=false;
            if (projectionChanged && projectionRevision_!=std::numeric_limits<std::uint64_t>::max()) ++projectionRevision_;
            const auto *document=document_->document();
            const auto count=document?std::int64_t(document->timeline.frameCount):0;
            auto next=options.get(); next.frame_count=count; next.enabled=count>0;
            next.playhead=count?std::clamp(document_->frame.get(),std::int64_t{0},count-1):0;
            next.revision=projectionRevision_;
            (void)options.set(std::move(next));
            projectionChanged=std::exchange(pendingProjection_,false);
        } while (std::exchange(pending_,false));
        synchronizing_=false;
    } catch (...) {synchronizing_=false;throw;}
}
ii::core::Result<ii::gui::EditorTimelineProjection> TimelinePanelModel::project(const ii::gui::EditorTimelineQuery &query) const {
    using namespace ii::gui;
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Timeline requires its owner thread");
    if (!std::isfinite(query.top) || !std::isfinite(query.bottom) || query.top<0 || query.bottom<query.top ||
        query.frames.first<0 || query.frames.end<query.frames.first)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid timeline viewport");
    EditorTimelineProjection output;
    const auto *document=document_->document();
    if (!document || query.bottom<=query.top) return output;
    const auto visualCount=document->layers.size();
    const auto total=visualCount+document->audioTracks.size();
    const auto first=static_cast<std::size_t>(std::min<double>(total,std::floor(query.top/rowHeight_)));
    const auto end=static_cast<std::size_t>(std::min<double>(total,std::ceil(query.bottom/rowHeight_)));
    if (end-first>4096) return ii::core::fail(ErrorCode::out_of_range,"Too many visible timeline tracks");
    const auto selected=document_->selectedLayer.get();
    const auto frameCount=std::int64_t(document->timeline.frameCount);
    const auto frameFirst=std::min(query.frames.first,frameCount),frameEnd=std::min(query.frames.end,frameCount);
    for (auto index=first;index<end;++index) {
        if (index>=visualCount) {
            const auto audioIndex=index-visualCount;
            const auto &audio=document->audioTracks[audioIndex];
            const auto track="audio/"+std::to_string(audioIndex);
            output.tracks.push_back({track,displayName(audio.name,"Audio"),double(index)*rowHeight_,rowHeight_});
            const auto firstClip=std::lower_bound(audio.clips.begin(),audio.clips.end(),frameFirst,
                [](const auto &clip,std::int64_t frame) {return std::int64_t(clip.startFrame)+clip.durationFrames<=frame;});
            const bool preview=gesture_ && gesture_->audio && gesture_->index==audioIndex &&
                gesture_->generation==document_->documentGeneration.get() && gesture_->revision==document_->revision.get();
            bool previewVisited=false;
            auto append=[&](std::size_t clipIndex) {
                const auto &clip=audio.clips[clipIndex];
                auto begin=std::int64_t(clip.startFrame),endFrame=begin+clip.durationFrames;
                if (preview && gesture_->audioClipIndex==clipIndex) {
                    previewVisited=true;begin=gesture_->previewFirst;endFrame=gesture_->previewEnd;
                }
                if (begin>=frameEnd || endFrame<=frameFirst) return true;
                if (output.clips.size()==4096) return false;
                output.clips.push_back({"audioClip/"+std::to_string(audioIndex)+"/"+std::to_string(clipIndex),track,
                    displayName(clip.name,"Audio clip"),begin,endFrame,false,!audioClipEdit_});
                return true;
            };
            for (auto clip=firstClip;clip!=audio.clips.end() && std::int64_t(clip->startFrame)<frameEnd;++clip)
                if (!append(static_cast<std::size_t>(clip-audio.clips.begin())))
                    return ii::core::fail(ErrorCode::out_of_range,"Too many visible timeline clips");
            if (preview && !previewVisited && gesture_->audioClipIndex<audio.clips.size() && !append(gesture_->audioClipIndex))
                return ii::core::fail(ErrorCode::out_of_range,"Too many visible timeline clips");
            continue;
        }
        const auto &layer=document->layers[index]; const auto &properties=iiSharedCanvas::layerProperties(layer);
        auto track="layer/"+std::to_string(index); auto label=displayName(properties.name);
        output.tracks.push_back({track,label,double(index)*rowHeight_,rowHeight_});
        auto begin=properties.frameRange?std::int64_t(properties.frameRange->firstFrame):0;
        auto endFrame=properties.frameRange?std::int64_t(properties.frameRange->lastFrame)+1:frameCount;
        std::int64_t previewShift=0;
        if (gesture_ && !gesture_->audio && gesture_->index==index && gesture_->generation==document_->documentGeneration.get() &&
            gesture_->revision==document_->revision.get()) {
            begin=gesture_->previewFirst; endFrame=gesture_->previewEnd;
            if (gesture_->kind==EditorTimelineGesture::move_clip) previewShift=begin-gesture_->first;
        }
        if (begin<frameEnd && endFrame>frameFirst) {
            if (output.clips.size()==4096) return ii::core::fail(ErrorCode::out_of_range,"Too many visible timeline clips");
            output.clips.push_back({"clip/"+std::to_string(index),track,label,begin,endFrame,properties.id==selected,!editableRange(layer)});
        }
        const bool movingKey=gesture_ && !gesture_->audio && gesture_->kind==EditorTimelineGesture::move_key && gesture_->index==index &&
            gesture_->generation==document_->documentGeneration.get() && gesture_->revision==document_->revision.get();
        auto add=[&](std::string kind,iiSharedCanvas::FrameIndex original,std::int64_t frame,bool preview=false) -> bool {
            if (movingKey && gesture_->keyOriginal==std::int64_t(original) &&
                ((kind=="source" && gesture_->keyKind==native::LayerKeyKind::source) ||
                 (kind=="motion" && gesture_->keyKind==native::LayerKeyKind::motion))) {
                if (!preview) return true;
                frame=gesture_->previewKey;
            }
            if (frame<frameFirst || frame>=frameEnd || frame<begin || frame>=endFrame) return true;
            if (output.keys.size()==4096) return false;
            output.keys.push_back({std::move(kind)+"/"+std::to_string(index)+"/"+std::to_string(original),track,frame});
            return true;
        };
        if (movingKey && !add(gesture_->keyKind==native::LayerKeyKind::source?"source":"motion",
                              static_cast<iiSharedCanvas::FrameIndex>(gesture_->keyOriginal),gesture_->previewKey,true))
            return ii::core::fail(ErrorCode::out_of_range,"Too many visible timeline keys");
        if (const auto *source=iiSharedCanvas::keyframedLayerSource(layer)) {
            const auto origin=std::clamp(frameFirst-previewShift,std::int64_t{0},frameCount);
            auto first=source->frameIndices.begin();
            if (first!=source->frameIndices.end() && *first==0) {
                if (!add("source",*first,0)) return ii::core::fail(ErrorCode::out_of_range,"Too many visible timeline keys");
                ++first;
            }
            auto at=std::lower_bound(first,source->frameIndices.end(),static_cast<iiSharedCanvas::FrameIndex>(origin));
            for (;at!=source->frameIndices.end() && std::int64_t(*at)+previewShift<frameEnd;++at)
                if (!add("source",*at,std::int64_t(*at)+previewShift)) return ii::core::fail(ErrorCode::out_of_range,"Too many visible timeline keys");
        }
        const auto origin=std::clamp(frameFirst-previewShift,std::int64_t{0},frameCount);
        auto motion=std::lower_bound(properties.motion.begin(),properties.motion.end(),origin,
            [](const auto &key,std::int64_t frame){return key.frame<frame;});
        for (;motion!=properties.motion.end() && std::int64_t(motion->frame)+previewShift<frameEnd;++motion)
            if (!add("motion",motion->frame,std::int64_t(motion->frame)+previewShift)) return ii::core::fail(ErrorCode::out_of_range,"Too many visible timeline keys");
    }
    return output;
}
ii::core::Result<void> TimelinePanelModel::input(const ii::gui::EditorTimelineEvent &event) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Timeline requires its owner thread");
    using ii::gui::EditorTimelineGesture;
    using ii::gui::EditorTimelinePhase;
    if (event.phase!=EditorTimelinePhase::begin && event.phase!=EditorTimelinePhase::update &&
        event.phase!=EditorTimelinePhase::end && event.phase!=EditorTimelinePhase::cancel)
        return ii::core::fail(ErrorCode::invalid_argument,"Unknown timeline gesture phase");
    if (event.kind==EditorTimelineGesture::scrub) {
        if (event.phase==EditorTimelinePhase::cancel) return {};
        if (gesture_) return ii::core::fail(ErrorCode::busy,"A timeline edit is in progress");
        auto result=document_->seek(event.frame);
        if (result) (void)status.set({}); else (void)status.set(result.error().message);
        return result;
    }
    if (event.kind==EditorTimelineGesture::move_key) {
        if (event.phase==EditorTimelinePhase::cancel) {
            if (gesture_) {gesture_.reset();synchronize(true);}
            return {};
        }
        if (event.phase==EditorTimelinePhase::begin) {
            if (gesture_) return ii::core::fail(ErrorCode::busy,"A timeline edit is already in progress");
            const auto separator=event.id.find('/'),second=separator==std::string::npos?separator:event.id.find('/',separator+1);
            if (separator==std::string::npos || second==std::string::npos)
                return ii::core::fail(ErrorCode::invalid_argument,"Unknown timeline keyframe");
            const auto kind=std::string_view(event.id).substr(0,separator);
            if (kind!="source" && kind!="motion") return ii::core::fail(ErrorCode::invalid_argument,"Unknown timeline keyframe");
            const auto indexText=std::string_view(event.id).substr(separator+1,second-separator-1);
            const auto frameText=std::string_view(event.id).substr(second+1);
            std::size_t index=0;std::int64_t original=0;
            const auto parsedIndex=std::from_chars(indexText.data(),indexText.data()+indexText.size(),index);
            const auto parsedFrame=std::from_chars(frameText.data(),frameText.data()+frameText.size(),original);
            const auto *document=document_->document();
            if (indexText.empty() || frameText.empty() || parsedIndex.ec!=std::errc{} || parsedFrame.ec!=std::errc{} ||
                parsedIndex.ptr!=indexText.data()+indexText.size() || parsedFrame.ptr!=frameText.data()+frameText.size() ||
                !document || index>=document->layers.size() || original<0 || original>=std::int64_t(document->timeline.frameCount) ||
                event.frame<0 || event.frame>=std::int64_t(document->timeline.frameCount))
                return ii::core::fail(ErrorCode::invalid_argument,"Timeline keyframe is unavailable");
            const auto &layer=document->layers[index];const auto &properties=iiSharedCanvas::layerProperties(layer);
            const auto count=std::int64_t(document->timeline.frameCount);
            const auto first=properties.frameRange?std::int64_t(properties.frameRange->firstFrame):0;
            const auto end=properties.frameRange?std::int64_t(properties.frameRange->lastFrame)+1:count;
            if (!editableRange(layer) || original<first || original>=end)
                return ii::core::fail(ErrorCode::unsupported,"This keyframe cannot be moved");
            const auto keyKind=kind=="source"?native::LayerKeyKind::source:native::LayerKeyKind::motion;
            if (keyKind==native::LayerKeyKind::source) {
                const auto *source=iiSharedCanvas::keyframedLayerSource(layer);
                if (!source || original==0 || !std::binary_search(source->frameIndices.begin(),source->frameIndices.end(),
                                                                  static_cast<iiSharedCanvas::FrameIndex>(original)))
                    return ii::core::fail(ErrorCode::unsupported,"The source keyframe is unavailable or is the frame-zero anchor");
            } else if (std::ranges::none_of(properties.motion,[&](const auto &key){return key.frame==original;}))
                return ii::core::fail(ErrorCode::invalid_argument,"Motion keyframe is unavailable");
            const auto id=properties.id;
            const auto generation=document_->documentGeneration.get(),revision=document_->revision.get();
            if (auto selected=document_->selectLayer(id); !selected) return selected;
            if (document_->documentGeneration.get()!=generation || document_->revision.get()!=revision || document_->selectedLayer.get()!=id)
                return ii::core::fail(ErrorCode::cancelled,"The canvas changed before the keyframe edit began");
            gesture_=Gesture{event.kind,event.id,id,index,generation,revision,first,end,event.frame,first,end,0,0};
            gesture_->keyOriginal=original;gesture_->previewKey=original;gesture_->keyKind=keyKind;
            (void)status.set({});return {};
        }
        if (!gesture_ || gesture_->clip!=event.id || gesture_->kind!=event.kind)
            return ii::core::fail(ErrorCode::invalid_argument,"Timeline keyframe edit is no longer active");
        if (gesture_->generation!=document_->documentGeneration.get() || gesture_->revision!=document_->revision.get() ||
            gesture_->layer!=document_->selectedLayer.get() || !document_->document()) {
            gesture_.reset();synchronize(true);(void)status.set("Canvas changed during the keyframe edit");
            return ii::core::fail(ErrorCode::cancelled,status.get());
        }
        const auto count=std::int64_t(document_->document()->timeline.frameCount);
        if (event.frame<0 || event.frame>=count) return ii::core::fail(ErrorCode::out_of_range,"Frame is outside the timeline");
        const auto first=gesture_->keyKind==native::LayerKeyKind::source?std::max(std::int64_t{1},gesture_->first):gesture_->first;
        const auto target=std::clamp(gesture_->keyOriginal+event.frame-gesture_->initial,first,gesture_->end-1);
        if (event.phase==EditorTimelinePhase::update) {
            if (target!=gesture_->previewKey) {gesture_->previewKey=target;synchronize(true);}
            return {};
        }
        const auto id=gesture_->layer;const auto original=gesture_->keyOriginal;const auto kind=gesture_->keyKind;
        gesture_.reset();
        auto result=document_->moveLayerKey(id,kind,original,target);
        if (!result) {(void)status.set(result.error().message);synchronize(true);return result;}
        (void)status.set({});synchronize(true);return {};
    }
    if (event.kind!=EditorTimelineGesture::move_clip && event.kind!=EditorTimelineGesture::trim_start &&
        event.kind!=EditorTimelineGesture::trim_end) {
        (void)status.set("Keyframe editing is unavailable"); return ii::core::fail(ErrorCode::unsupported,status.get());
    }
    if (event.phase==EditorTimelinePhase::cancel) {
        if (gesture_) {gesture_.reset();synchronize(true);}
        return {};
    }
    if (event.id.starts_with("audioClip/")) {
        if (event.phase==EditorTimelinePhase::begin) {
            if (gesture_) return ii::core::fail(ErrorCode::busy,"A timeline edit is already in progress");
            if (!audioClipEdit_) return ii::core::fail(ErrorCode::unsupported,"Audio editing is unavailable");
            const auto suffix=std::string_view(event.id).substr(10);const auto separator=suffix.find('/');
            if (separator==std::string_view::npos) return ii::core::fail(ErrorCode::invalid_argument,"Unknown audio clip");
            const auto trackText=suffix.substr(0,separator),clipText=suffix.substr(separator+1);
            std::size_t trackIndex=0,clipIndex=0;
            const auto parsedTrack=std::from_chars(trackText.data(),trackText.data()+trackText.size(),trackIndex);
            const auto parsedClip=std::from_chars(clipText.data(),clipText.data()+clipText.size(),clipIndex);
            const auto *document=document_->document();
            if (trackText.empty() || clipText.empty() || parsedTrack.ec!=std::errc{} || parsedClip.ec!=std::errc{} ||
                parsedTrack.ptr!=trackText.data()+trackText.size() || parsedClip.ptr!=clipText.data()+clipText.size() ||
                !document || trackIndex>=document->audioTracks.size() ||
                clipIndex>=document->audioTracks[trackIndex].clips.size() || event.frame<0 ||
                event.frame>=std::int64_t(document->timeline.frameCount))
                return ii::core::fail(ErrorCode::invalid_argument,"Audio clip is unavailable");
            const auto &track=document->audioTracks[trackIndex];const auto &clip=track.clips[clipIndex];
            const auto first=std::int64_t(clip.startFrame),end=first+clip.durationFrames;
            const auto left=clipIndex?std::int64_t(track.clips[clipIndex-1].startFrame)+track.clips[clipIndex-1].durationFrames:0;
            const auto right=clipIndex+1<track.clips.size()?std::int64_t(track.clips[clipIndex+1].startFrame):
                std::int64_t(document->timeline.frameCount);
            gesture_=Gesture{event.kind,event.id,{},trackIndex,document_->documentGeneration.get(),document_->revision.get(),
                first,end,event.frame,first,end,left-first,right-end};
            gesture_->audio=true;gesture_->audioClipIndex=clipIndex;gesture_->audioTrack=track.id;gesture_->audioClip=clip.id;
            (void)status.set({});return {};
        }
        if (!gesture_ || !gesture_->audio || gesture_->clip!=event.id || gesture_->kind!=event.kind)
            return ii::core::fail(ErrorCode::invalid_argument,"Audio timeline edit is no longer active");
        if (!audioClipEdit_ || !document_->document() || gesture_->generation!=document_->documentGeneration.get() ||
            gesture_->revision!=document_->revision.get()) {
            gesture_.reset();synchronize(true);(void)status.set("Canvas changed during the audio edit");
            return ii::core::fail(ErrorCode::cancelled,status.get());
        }
        if (event.frame<0 || event.frame>=std::int64_t(document_->document()->timeline.frameCount))
            return ii::core::fail(ErrorCode::out_of_range,"Frame is outside the timeline");
        auto first=gesture_->first,end=gesture_->end;
        if (event.kind==EditorTimelineGesture::move_clip) {
            const auto shift=std::clamp(event.frame-gesture_->initial,gesture_->minOffset,gesture_->maxOffset);
            first+=shift;end+=shift;
        } else if (event.kind==EditorTimelineGesture::trim_start) first=std::clamp(event.frame,first,end-1);
        else end=std::clamp(event.frame+1,first+1,end);
        if (event.phase==EditorTimelinePhase::update) {
            if (gesture_->previewFirst!=first || gesture_->previewEnd!=end) {
                gesture_->previewFirst=first;gesture_->previewEnd=end;synchronize(true);
            }
            return {};
        }
        auto track=gesture_->audioTrack,clip=gesture_->audioClip;const auto kind=gesture_->kind;
        const bool changed=first!=gesture_->first || end!=gesture_->end;
        gesture_.reset();synchronize(true);
        if (!changed) return {};
        auto result=audioClipEdit_(std::move(track),std::move(clip),kind,first,end);
        if (!result) (void)status.set(result.error().message);
        return result;
    }
    if (event.phase==EditorTimelinePhase::begin) {
        if (gesture_) return ii::core::fail(ErrorCode::busy,"A timeline edit is already in progress");
        if (!event.id.starts_with("clip/")) return ii::core::fail(ErrorCode::invalid_argument,"Unknown timeline clip");
        const auto suffix=std::string_view(event.id).substr(5); std::size_t index=0;
        const auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),index);
        const auto *document=document_->document();
        if (suffix.empty() || parsed.ec!=std::errc{} || parsed.ptr!=suffix.data()+suffix.size() ||
            !document || index>=document->layers.size()) return ii::core::fail(ErrorCode::invalid_argument,"Timeline clip is unavailable");
        const auto count=std::int64_t(document->timeline.frameCount);
        if (event.frame<0 || event.frame>=count || !editableRange(document->layers[index]))
            return ii::core::fail(ErrorCode::unsupported,"This timeline clip cannot be moved or trimmed");
        const auto &properties=iiSharedCanvas::layerProperties(document->layers[index]);
        const auto id=properties.id;
        const auto first=properties.frameRange?std::int64_t(properties.frameRange->firstFrame):0;
        const auto end=properties.frameRange?std::int64_t(properties.frameRange->lastFrame)+1:count;
        auto minOffset=-first,maxOffset=count-end;
        if (const auto *source=iiSharedCanvas::keyframedLayerSource(document->layers[index])) {
            if (source->frameIndices.size()>1) {
                minOffset=std::max(minOffset,std::int64_t{1}-std::int64_t(source->frameIndices[1]));
                maxOffset=std::min(maxOffset,count-1-std::int64_t(source->frameIndices.back()));
            }
        }
        if (!properties.motion.empty()) {
            minOffset=std::max(minOffset,-std::int64_t(properties.motion.front().frame));
            maxOffset=std::min(maxOffset,count-1-std::int64_t(properties.motion.back().frame));
        }
        const auto generation=document_->documentGeneration.get(),revision=document_->revision.get();
        if (auto selected=document_->selectLayer(id); !selected) return selected;
        if (document_->documentGeneration.get()!=generation || document_->revision.get()!=revision || document_->selectedLayer.get()!=id)
            return ii::core::fail(ErrorCode::cancelled,"The canvas changed before the timeline edit began");
        gesture_=Gesture{event.kind,event.id,id,index,generation,revision,first,end,event.frame,first,end,minOffset,maxOffset};
        (void)status.set({}); return {};
    }
    if (!gesture_ || gesture_->clip!=event.id || gesture_->kind!=event.kind)
        return ii::core::fail(ErrorCode::invalid_argument,"Timeline edit is no longer active");
    if (gesture_->generation!=document_->documentGeneration.get() || gesture_->revision!=document_->revision.get() ||
        gesture_->layer!=document_->selectedLayer.get() || !document_->document()) {
        gesture_.reset(); synchronize(true); (void)status.set("Canvas changed during the timeline edit");
        return ii::core::fail(ErrorCode::cancelled,status.get());
    }
    const auto count=std::int64_t(document_->document()->timeline.frameCount);
    if (event.frame<0 || event.frame>=count) return ii::core::fail(ErrorCode::out_of_range,"Frame is outside the timeline");
    auto first=gesture_->first,end=gesture_->end;
    if (event.kind==EditorTimelineGesture::move_clip) {
        const auto delta=std::clamp(event.frame-gesture_->initial,gesture_->minOffset,gesture_->maxOffset);
        first+=delta; end+=delta;
    } else if (event.kind==EditorTimelineGesture::trim_start) first=std::clamp(event.frame,std::int64_t{0},end-1);
    else end=std::clamp(event.frame+1,first+1,count);
    if (event.phase==EditorTimelinePhase::update) {
        if (gesture_->previewFirst!=first || gesture_->previewEnd!=end) {
            gesture_->previewFirst=first; gesture_->previewEnd=end; synchronize(true);
        }
        return {};
    }
    const auto id=gesture_->layer; const auto kind=gesture_->kind; gesture_.reset();
    auto result=kind==EditorTimelineGesture::move_clip?document_->moveLayerRange(id,first,end):document_->setLayerRange(id,first,end);
    if (!result) {(void)status.set(result.error().message);synchronize(true);return result;}
    (void)status.set({}); synchronize(true); return {};
}
void TimelinePanelModel::sizeChanged(double width,double height) {
    if (owner_!=std::this_thread::get_id() || !std::isfinite(width) || !std::isfinite(height) ||
        width<0 || height<0 || width>8192 || height>8192) return;
    width=std::max(1.0,width); height=std::max(1.0,height);
    (void)this->width.set(width); (void)this->height.set(height);
    auto next=options.get();next.width=width;next.height=height;(void)options.set(std::move(next));
}
void TimelinePanelModel::viewportChanged(const ii::gui::EditorTimelineOptions &value) {
    if (owner_!=std::this_thread::get_id() || !std::isfinite(value.pixels_per_frame) || value.pixels_per_frame<.0001 ||
        value.pixels_per_frame>8192 || !std::isfinite(value.scroll_x) || value.scroll_x<0 ||
        !std::isfinite(value.scroll_y) || value.scroll_y<0 || value.scroll_y>1e12) return;
    auto next=options.get();next.pixels_per_frame=value.pixels_per_frame;next.scroll_x=value.scroll_x;next.scroll_y=value.scroll_y;
    (void)options.set(std::move(next));
}
}
