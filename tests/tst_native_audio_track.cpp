#include "App/Views/Editor/NativeAudioTrack.h"
#include "App/Native/NativeDocumentSession.h"
#include <Validation/Validation.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;using namespace iiSharedCanvas;
        auto session=native::DocumentSession::create();Document doc;doc.extent={4,4};doc.timeline={{24,1},24};
        doc.audioTracks.push_back({.id="track",.name="Audio"});good(session->attach(doc,"audio.iisc"));
        check(!editor::prepareAudioTrackCreate(session->snapshot(),session->documentGeneration.get(),session->revision.get(),[]{return true;}),
              "a cancelled audio operation continued");
        AudioAsset source{"input",48000,1,std::vector<std::int16_t>(48001,1234)};
        const auto before=session->snapshot();const auto visual=session->visualRevision.get();
        auto imported=editor::prepareAudioImport(before,session->documentGeneration.get(),session->revision.get(),
                                                  "track",6,source,"Voice");
        check(bool(imported) && session->snapshot()==before && !session->dirty.get(),"audio preparation edited the live document");
        good(session->commitAudioEdit(std::move(*imported)));
        const auto &track=session->document()->audioTracks.front();
        const auto *asset=findAudioAsset(*session->document(),track.clips.front().assetId);
        check(asset && asset->sampleRate==48000 && asset->channelCount==1 && asset->samples.size()==50000 &&
              asset->samples[48000]==1234 && asset->samples.back()==0 && track.clips.front().startFrame==6 &&
              track.clips.front().durationFrames==25 && session->document()->timeline.frameCount==31 &&
              session->frameCount.get()==31 && session->visualRevision.get()==visual && session->dirty.get(),
              "audio import lost samples, timing, frame extension or source format");
        check(before->audioAssets.empty() && before->audioTracks.front().clips.empty(),"the immutable source snapshot changed");
        auto overlap=editor::prepareAudioImport(session->snapshot(),session->documentGeneration.get(),session->revision.get(),
                                                 "track",6,source,"Overlapping");
        check(!overlap,"overlapping audio clips were accepted");
        auto gain=editor::prepareAudioTrackPatch(session->snapshot(),session->documentGeneration.get(),session->revision.get(),
                                                 "track",true,-6.0);
        check(bool(gain),"a valid audio track edit was rejected");good(session->commitAudioEdit(std::move(*gain)));
        check(session->document()->audioTracks.front().muted && session->document()->audioTracks.front().gainDb==-6.0 &&
              session->visualRevision.get()==visual,"mute or gain did not update the document without repainting it");
        const auto clipId=session->document()->audioTracks.front().clips.front().id;
        const auto sourceAsset=session->document()->audioAssets.front();
        auto trim=editor::prepareAudioClipTrim(session->snapshot(),session->documentGeneration.get(),session->revision.get(),
                                               "track",clipId,8,25);
        check(bool(trim),"valid audio clip trim was rejected");good(session->commitAudioEdit(std::move(*trim)));
        const auto &trimmed=session->document()->audioTracks.front().clips.front();
        check(trimmed.startFrame==8 && trimmed.durationFrames==17 && trimmed.sourceOffsetSamples==4000 &&
              session->document()->audioAssets.front()==sourceAsset && session->visualRevision.get()==visual,
              "audio trim changed its asset or did not preserve clip timing");
        check(!editor::prepareAudioClipTrim(session->snapshot(),session->documentGeneration.get(),session->revision.get(),
                                             "track",clipId,7,25),"audio trim unexpectedly expanded the clip");
        auto adjacent=*session->document();
        adjacent.audioTracks.front().clips.push_back({.id="adjacent",.name="Adjacent",
            .assetId=sourceAsset.id,.startFrame=26,.durationFrames=3});
        check(validate(adjacent).ok(),"adjacent audio fixture is invalid");
        auto adjacentSnapshot=std::make_shared<const Document>(std::move(adjacent));
        check(!editor::prepareAudioClipMove(adjacentSnapshot,session->documentGeneration.get(),session->revision.get(),
                                           "track",clipId,10),"moving audio into another clip was accepted");
        auto moved=editor::prepareAudioClipMove(adjacentSnapshot,session->documentGeneration.get(),session->revision.get(),
                                                "track",clipId,0);
        check(moved && moved->candidate && moved->candidate->audioTracks.front().clips.front().startFrame==0 &&
              adjacentSnapshot->audioTracks.front().clips.front().startFrame==8,
              "audio move did not preserve its immutable source");
        check(!editor::prepareAudioTrackPatch(session->snapshot(),session->documentGeneration.get(),session->revision.get(),
                                               "track",false,1000),"an invalid gain was accepted");
        auto stale=editor::prepareAudioTrackPatch(session->snapshot(),session->documentGeneration.get(),session->revision.get(),
                                                   "track",false,0);
        check(bool(stale),"stale-edit fixture failed");good(session->editPublication("title","Newer"));
        const auto changed=session->snapshot();
        check(!session->commitAudioEdit(std::move(*stale)) && session->snapshot()==changed,
              "a stale audio edit overwrote a newer document");
        check(!editor::prepareAudioImport(changed,session->documentGeneration.get(),session->revision.get(),
                                           "unknown",0,source,"Voice"),"an unknown track accepted audio");
        source.channelCount=0;
        check(!editor::prepareAudioImport(changed,session->documentGeneration.get(),session->revision.get(),
                                           "track",0,source,"Voice"),"invalid audio was accepted");
        auto rational=native::DocumentSession::create();Document fractional;fractional.extent={2,2};
        fractional.timeline={{30000,1001},1};fractional.audioTracks.push_back({.id="rational",.name="Rational"});
        good(rational->attach(fractional,"rational.iisc"));
        AudioAsset stereo{"source",48000,2,std::vector<std::int16_t>(48001*2,321)};
        auto converted=editor::prepareAudioImport(rational->snapshot(),rational->documentGeneration.get(),rational->revision.get(),
                                                   "rational",0,std::move(stereo),"Stereo");
        check(bool(converted),"fractional frame-rate import failed");good(rational->commitAudioEdit(std::move(*converted)));
        const auto &clip=rational->document()->audioTracks.front().clips.front();
        const auto *stereoAsset=findAudioAsset(*rational->document(),clip.assetId);
        check(clip.durationFrames==30 && stereoAsset && stereoAsset->channelCount==2 && stereoAsset->samples.size()==48048*2 &&
              stereoAsset->samples[48001*2-1]==321 && stereoAsset->samples.back()==0,
              "rational-rate placement or sub-frame padding was inaccurate");
        auto fractionalTrim=editor::prepareAudioClipTrim(rational->snapshot(),rational->documentGeneration.get(),rational->revision.get(),
                                                         "rational",clip.id,1,25);
        check(bool(fractionalTrim),"fractional-rate trim failed");good(rational->commitAudioEdit(std::move(*fractionalTrim)));
        check(rational->document()->audioTracks.front().clips.front().sourceOffsetSamples==1601,
              "fractional source offset was not rounded down to the source sample");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
