#include "App/Views/Editor/NativeTimelinePanel.h"
#include <Document/Document.h>
#include <Serialization/IiscCodec.h>
#include <Validation/Validation.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
template<class T> T take(ii::core::Result<T> value) {if (!value) throw std::runtime_error(value.error().message);return std::move(*value);}
int main() {
    try {
        using namespace dreamscapes;
        using namespace iiSharedCanvas;
        using namespace ii::gui;
        auto session=native::DocumentSession::create();
        auto timeline=editor::TimelinePanelModel::create(session,22);
        check(!session->createAudioTrack(),"an empty session allowed a new audio track");
        check(timeline->options.get().frame_count==0 &&
              timeline->options.get().frame_rate.numerator==0 &&
              take(timeline->project({.frames={0,0},.top=0,.bottom=100})).tracks.empty(),
              "an empty session displayed fabricated tracks");
        Document document; document.extent={2,2}; document.timeline={{24,1},24};
        document.assets.emplace_back(RasterAsset{"one",makeRasterLayer(2,2,0xff000000)});
        document.assets.emplace_back(RasterAsset{"two",makeRasterLayer(2,2,0xffffffff)});
        document.layers.emplace_back(StaticBitmapLayer{{"still","Still",true,1,{},RasterBlendMode::SourceOver,
                                                        LayerFrameRange{2,9}},StaticSource{"one"}});
        document.layers.emplace_back(DynamicBitmapLayer{{"motion","Motion"},KeyframedSource{{0,12}}});
        document.frames={{0,{{"motion","one"}}},{12,{{"motion","two"}}}};
        good(session->attach(document,"timeline.iisc"));
        check(timeline->options.get().frame_rate==EditorTimelineFrameRate{24,1},
              "The timeline did not read the canvas project frame rate");
        auto fractional=document;
        fractional.timeline.frameRate={30000,1001};
        good(session->attach(std::move(fractional),"fractional.iisc"));
        check(timeline->options.get().frame_count==24 &&
              timeline->options.get().frame_rate==EditorTimelineFrameRate{30000,1001},
              "The timeline did not update its exact frame rate when the canvas changed");
        good(session->attach(document,"timeline.iisc"));
        auto projection=take(timeline->project({.frames={0,24},.top=0,.bottom=44}));
        check(projection.tracks.size()==2 && projection.clips.size()==2 && projection.keys.size()==2 &&
              projection.tracks[0].label=="Still" && projection.clips[0].first==2 && projection.clips[0].end==10 &&
              projection.tracks[1].label=="Motion" && projection.keys[1].frame==12,
              "timeline did not project real layer ranges and dynamic source frames");
        auto cropped=take(timeline->project({.frames={10,20},.top=22,.bottom=44}));
        check(cropped.tracks.size()==1 && cropped.tracks[0].label=="Motion" && cropped.keys.size()==1 &&
              cropped.keys[0].frame==12,"timeline queried offscreen tracks or frames");
        const auto revision=session->revision.get();
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::scrub,.frame=0}));
        good(timeline->input({.phase=EditorTimelinePhase::update,.kind=EditorTimelineGesture::scrub,.frame=7}));
        good(timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::scrub,.frame=8}));
        check(session->frame.get()==8 && timeline->options.get().playhead==8 && !session->dirty.get() &&
              session->revision.get()==revision,"scrubbing edited the canvas or failed to update the playhead");
        check(!timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_clip,.id="unknown",.frame=3}) &&
              session->revision.get()==revision,"unsupported drag changed the document");
        good(session->selectLayer("motion"));
        projection=take(timeline->project({.frames={0,24},.top=0,.bottom=44}));
        check(projection.clips[1].selected && !projection.clips[0].selected,
              "timeline selection did not follow the document session");
        const auto original=session->snapshot();
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::trim_end,
                              .id="clip/0",.frame=9,.initial_frame=9}));
        good(timeline->input({.phase=EditorTimelinePhase::update,.kind=EditorTimelineGesture::trim_end,
                              .id="clip/0",.frame=6,.initial_frame=9}));
        projection=take(timeline->project({.frames={0,24},.top=0,.bottom=44}));
        check(projection.clips[0].end==7 && !session->dirty.get() && session->snapshot()==original,
              "drag preview modified the source document or failed to show the new range");
        good(timeline->input({.phase=EditorTimelinePhase::cancel,.kind=EditorTimelineGesture::trim_end,
                              .id="clip/0",.frame=6,.initial_frame=9}));
        projection=take(timeline->project({.frames={0,24},.top=0,.bottom=44}));
        check(projection.clips[0].end==10 && !session->dirty.get(),"cancel did not restore the original range");
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::trim_end,
                              .id="clip/0",.frame=9,.initial_frame=9}));
        good(timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::trim_end,
                              .id="clip/0",.frame=6,.initial_frame=9}));
        const auto range=layerProperties(session->document()->layers[0]).frameRange;
        check(range && range->firstFrame==2 && range->lastFrame==6 && session->dirty.get() &&
              session->revision.get()==revision+1 && layerProperties(original->layers[0]).frameRange->lastFrame==9,
              "completed trim did not edit only the selected static layer");
        good(session->attach(document,"move.iisc"));
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_clip,
                              .id="clip/0",.frame=4,.initial_frame=4}));
        good(timeline->input({.phase=EditorTimelinePhase::update,.kind=EditorTimelineGesture::move_clip,
                              .id="clip/0",.frame=7,.initial_frame=4}));
        projection=take(timeline->project({.frames={0,24},.top=0,.bottom=44}));
        check(projection.clips[0].first==5 && projection.clips[0].end==13 && !session->dirty.get(),
              "moving a clip did not preview the bounded range");
        good(timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::move_clip,
                              .id="clip/0",.frame=7,.initial_frame=4}));
        check(layerProperties(session->document()->layers[0]).frameRange==std::optional<LayerFrameRange>{{5,12}},
              "moving a clip did not commit the range");
        good(session->attach(document,"conflict.iisc"));
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::trim_end,
                              .id="clip/0",.frame=9,.initial_frame=9}));
        good(session->setLayerOpacity("still",0.5));
        check(!timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::trim_end,
                               .id="clip/0",.frame=6,.initial_frame=9}) &&
              layerProperties(session->document()->layers[0]).frameRange==std::optional<LayerFrameRange>{{2,9}},
              "a stale drag changed the document after another edit");
        auto animated=document;
        layerProperties(animated.layers[0]).motion={{1,{}},{6,{}},{20,{}}};
        good(session->attach(std::move(animated),"animated.iisc"));
        projection=take(timeline->project({.frames={4,8},.top=0,.bottom=22}));
        check(projection.keys.size()==1 && projection.keys[0].frame==6 && !projection.clips[0].locked,
              "motion keyframes were not bounded to the viewport or the clip was locked");
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::trim_end,
                              .id="clip/0",.frame=6,.initial_frame=6}));
        good(timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::trim_end,
                              .id="clip/0",.frame=6,.initial_frame=6}));
        check(layerProperties(session->document()->layers[0]).motion.size()==3,
              "trimming an animated layer destroyed motion keys outside its range");

        Document dynamic; dynamic.extent={2,2}; dynamic.timeline={{24,1},24};
        dynamic.assets=document.assets;
        dynamic.layers.emplace_back(DynamicBitmapLayer{{"dynamic","Dynamic",true,1,{},RasterBlendMode::SourceOver,
                                                         LayerFrameRange{3,14}},KeyframedSource{{0,4,12,16}}});
        layerProperties(dynamic.layers[0]).motion={{2,{}},{8,{}},{18,{}}};
        dynamic.frames={{0,{{"dynamic","one"}}},{4,{{"dynamic","two"}}},
                        {12,{{"dynamic","one"}}},{16,{{"dynamic","two"}}}};
        if (const auto valid=validate(dynamic); !valid.ok())
            throw std::runtime_error("dynamic fixture: "+valid.issues.front().path+": "+valid.issues.front().message);
        good(session->attach(dynamic,"dynamic.iisc"));
        const auto beforeDynamic=session->snapshot();
        const auto dynamicRevision=session->revision.get();
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_clip,
                              .id="clip/0",.frame=7,.initial_frame=7}));
        good(timeline->input({.phase=EditorTimelinePhase::update,.kind=EditorTimelineGesture::move_clip,
                              .id="clip/0",.frame=10,.initial_frame=7}));
        projection=take(timeline->project({.frames={0,24},.top=0,.bottom=22}));
        check(projection.clips[0].first==6 && projection.clips[0].end==18 &&
              std::ranges::any_of(projection.keys,[](const auto &key){return key.frame==7;}) &&
              std::ranges::any_of(projection.keys,[](const auto &key){return key.frame==11;}) &&
              session->snapshot()==beforeDynamic && !session->dirty.get(),
              "dynamic movement preview did not translate keys without modifying the document");
        good(timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::move_clip,
                              .id="clip/0",.frame=10,.initial_frame=7}));
        const auto &moved=session->document()->layers[0];
        check(layerProperties(moved).frameRange==std::optional<LayerFrameRange>{{6,17}} &&
              keyframedLayerSource(moved)->frameIndices==std::vector<FrameIndex>({0,7,15,19}) &&
              layerProperties(moved).motion[0].frame==5 && layerProperties(moved).motion[2].frame==21 &&
              session->revision.get()==dynamicRevision+1 && beforeDynamic->frames[0].index==0,
              "dynamic move lost source or motion keys or did not commit atomically");
        const auto *held=resolveAssetAt(*session->document(),moved,6);
        check(held && std::get<RasterAsset>(*held).id=="one", "dynamic move changed the held source image");
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::trim_end,
                              .id="clip/0",.frame=17,.initial_frame=17}));
        good(timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::trim_end,
                              .id="clip/0",.frame=11,.initial_frame=17}));
        check(keyframedLayerSource(session->document()->layers[0])->frameIndices==std::vector<FrameIndex>({0,7,15,19}) &&
              layerProperties(session->document()->layers[0]).motion.size()==3,
              "dynamic trim discarded hidden source or motion keys");
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::trim_end,
                              .id="clip/0",.frame=11,.initial_frame=11}));
        good(timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::trim_end,
                              .id="clip/0",.frame=17,.initial_frame=11}));
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_clip,
                              .id="clip/0",.frame=8,.initial_frame=8}));
        good(timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::move_clip,
                              .id="clip/0",.frame=20,.initial_frame=8}));
        check(layerProperties(session->document()->layers[0]).frameRange==std::optional<LayerFrameRange>{{8,19}} &&
              layerProperties(session->document()->layers[0]).motion.back().frame==23,
              "movement did not preserve off-range keys at the timeline boundary");
        const auto safe=session->snapshot();
        check(!session->moveLayerRange("dynamic",9,21) && session->snapshot()==safe,
              "an out-of-bounds hidden key modified the document");
        const auto encoded=encodeIisc(*session->document());
        check(encoded.ok(),"moved dynamic timeline could not be encoded");
        const auto decoded=decodeIisc(encoded.bytes);
        check(decoded.ok() && keyframedLayerSource(decoded.document.layers[0])->frameIndices==
              keyframedLayerSource(session->document()->layers[0])->frameIndices &&
              layerProperties(decoded.document.layers[0]).motion==layerProperties(session->document()->layers[0]).motion,
              "dynamic timeline keys changed on serialization round trip");
        auto keyFixture=dynamic;
        layerProperties(keyFixture.layers[0]).motion.insert(layerProperties(keyFixture.layers[0]).motion.begin()+2,{12,{}});
        good(session->attach(keyFixture,"keys.iisc"));
        const auto originalKeys=session->snapshot();const auto keysRevision=session->revision.get();
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_key,
                              .id="source/0/12",.frame=12,.initial_frame=12}));
        good(timeline->input({.phase=EditorTimelinePhase::update,.kind=EditorTimelineGesture::move_key,
                              .id="source/0/12",.frame=10,.initial_frame=12}));
        projection=take(timeline->project({.frames={9,12},.top=0,.bottom=22}));
        check(std::ranges::any_of(projection.keys,[](const auto &key){return key.id=="source/0/12" && key.frame==10;}) &&
              session->snapshot()==originalKeys && !session->dirty.get() && session->revision.get()==keysRevision,
              "source key drag did not project a transient key entering the viewport");
        good(timeline->input({.phase=EditorTimelinePhase::cancel,.kind=EditorTimelineGesture::move_key,
                              .id="source/0/12",.frame=10,.initial_frame=12}));
        check(session->snapshot()==originalKeys && !session->dirty.get(),"canceling a source key drag edited the document");
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_key,
                              .id="source/0/12",.frame=12,.initial_frame=12}));
        good(timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::move_key,
                              .id="source/0/12",.frame=10,.initial_frame=12}));
        check(keyframedLayerSource(session->document()->layers[0])->frameIndices==std::vector<FrameIndex>({0,4,10,16}) &&
              findKeyframe(*session->document(),"dynamic",10)->assetId=="one" && session->revision.get()==keysRevision+1,
              "source key move lost its asset or did not commit exactly once");
        const auto beforeCollision=session->snapshot();
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_key,
                              .id="source/0/10",.frame=10,.initial_frame=10}));
        check(!timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::move_key,
                               .id="source/0/10",.frame=4,.initial_frame=10}) && session->snapshot()==beforeCollision,
              "a source key collision changed the document");
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_key,
                              .id="motion/0/8",.frame=8,.initial_frame=8}));
        good(timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::move_key,
                              .id="motion/0/8",.frame=11,.initial_frame=8}));
        check(layerProperties(session->document()->layers[0]).motion[1].frame==11 &&
              layerProperties(session->document()->layers[0]).motion.size()==4,
              "motion key move lost animation data");
        const auto beforeMotionCollision=session->snapshot();
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_key,
                              .id="motion/0/11",.frame=11,.initial_frame=11}));
        check(!timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::move_key,
                               .id="motion/0/11",.frame=12,.initial_frame=11}) && session->snapshot()==beforeMotionCollision,
              "a motion key collision changed the document");
        const auto keyBytes=encodeIisc(*session->document());
        check(keyBytes.ok(),"moved keys could not be encoded");
        const auto reopenedKeys=decodeIisc(keyBytes.bytes);
        check(reopenedKeys.ok() && layerProperties(reopenedKeys.document.layers[0]).motion==
              layerProperties(session->document()->layers[0]).motion &&
              keyframedLayerSource(reopenedKeys.document.layers[0])->frameIndices==std::vector<FrameIndex>({0,4,10,16}),
              "moved keys did not round-trip through .iisc");
        good(session->attach(dynamic,"stale-key.iisc"));
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_key,
                              .id="motion/0/8",.frame=8,.initial_frame=8}));
        good(session->setLayerOpacity("dynamic",0.5));
        check(!timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::move_key,
                               .id="motion/0/8",.frame=11,.initial_frame=8}) &&
              layerProperties(session->document()->layers[0]).motion[1].frame==8,
              "a stale key drag edited a changed canvas");
        check(!timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_key,
                               .id="source/0/0",.frame=0,.initial_frame=0}),
              "the required source frame-zero anchor was movable");
        auto audio=document;
        audio.audioAssets.push_back(AudioAsset{"voice",24000,1,std::vector<std::int16_t>(6000)});
        audio.audioTracks.push_back(AudioTrackLayer{.id="narration",.name="Narration",.clips={
            AudioClip{.id="voice-clip",.name="Voice",.assetId="voice",.startFrame=3,.durationFrames=6}}});
        audio.audioTracks.push_back(AudioTrackLayer{.id="empty",.name="Empty audio"});
        check(validate(audio).ok(),"audio fixture is invalid");
        good(session->attach(audio,"audio.iisc"));
        const auto audioSnapshot=session->snapshot();const auto audioRevision=session->revision.get();
        projection=take(timeline->project({.frames={0,24},.top=44,.bottom=88}));
        check(projection.tracks.size()==2 && projection.tracks[0].label=="Narration" &&
              projection.tracks[1].label=="Empty audio" && projection.clips.size()==1 &&
              projection.clips[0].label=="Voice" && projection.clips[0].first==3 &&
              projection.clips[0].end==9 && projection.clips[0].locked,
              "timeline did not project authored audio tracks and clip ranges");
        const auto audioCrop=take(timeline->project({.frames={9,24},.top=44,.bottom=66}));
        check(audioCrop.tracks.size()==1 && audioCrop.clips.empty() && !session->dirty.get() &&
              session->revision.get()==audioRevision && session->snapshot()==audioSnapshot,
              "audio projection ignored the viewport or changed the source document");
        check(!timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_clip,
                               .id="audioClip/0/0",.frame=4}) && session->snapshot()==audioSnapshot,
              "a read-only audio row changed audio timing");
        const auto audioVisualRevision=session->visualRevision.get();
        const auto createdTrack=take(session->createAudioTrack());
        const auto *created=findAudioTrack(*session->document(),createdTrack);
        projection=take(timeline->project({.frames={0,24},.top=44,.bottom=110}));
        check(created && created->clips.empty() && projection.tracks.size()==3 &&
              projection.tracks[2].label==created->name && session->dirty.get() &&
              session->revision.get()==audioRevision+1 && session->visualRevision.get()==audioVisualRevision &&
              audioSnapshot->audioTracks.size()==2,
              "creating an audio track did not preserve the snapshot, timeline, or visual revision");
        const auto audioBytes=encodeIisc(*session->document());
        const auto reopenedAudio=decodeIisc(audioBytes.bytes);
        check(audioBytes.ok() && reopenedAudio.ok() && findAudioTrack(reopenedAudio.document,createdTrack),
              "the created audio track did not round-trip through .iisc");
        auto collisionDocument=audio;
        collisionDocument.audioTracks[0].id="audio.track.1";
        collisionDocument.audioTracks[0].name="Audio 1";
        layerProperties(collisionDocument.layers[0]).id="audio.track.2";
        good(session->attach(collisionDocument,"audio-collision.iisc"));
        const auto collisionTrack=take(session->createAudioTrack());
        check(collisionTrack=="audio.track.3" && findAudioTrack(*session->document(),collisionTrack)->name=="Audio 2",
              "audio track creation reused an existing layer identity or track name");
        auto switched=native::DocumentSession::create(); good(switched->attach(document,"Before.iisc"));
        Document replacement; replacement.extent={3,3};
        std::uint64_t replacementRevision=0,replacementVisual=0;
        bool replaced=false;
        auto observe=switched->dirty.observe([&](bool dirty,std::uint64_t) {
            if (dirty && !replaced) {
                replaced=true; good(switched->attach(replacement,"After.iisc"));
                replacementRevision=switched->revision.get();replacementVisual=switched->visualRevision.get();
            }
        });
        check(!switched->setLayerRange("still",2,7) && replaced && switched->title.get()=="After.iisc" &&
              !switched->dirty.get() && switched->revision.get()==replacementRevision &&
              switched->visualRevision.get()==replacementVisual,
              "a document switch during trim modified the replacement session");
        auto switchedAudio=native::DocumentSession::create();good(switchedAudio->attach(document,"Before audio.iisc"));
        bool nestedRejected=false,audioReplaced=false;
        auto audioObserve=switchedAudio->dirty.observe([&](bool dirty,std::uint64_t) {
            if (dirty && !audioReplaced) {
                nestedRejected=!switchedAudio->createAudioTrack();audioReplaced=true;
                good(switchedAudio->attach(replacement,"After audio.iisc"));
            }
        });
        check(!switchedAudio->createAudioTrack() && nestedRejected && audioReplaced &&
              switchedAudio->title.get()=="After audio.iisc" && !switchedAudio->dirty.get() &&
              switchedAudio->document()->audioTracks.empty(),
              "reentrant audio creation modified a replacement document or admitted a nested edit");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
