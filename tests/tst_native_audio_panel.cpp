#include "App/Views/Editor/NativeAudioPanel.h"
#include "App/Views/Editor/NativeEditorWorkspace.h"
#include "App/Native/NativeCanvasFiles.h"
#include <Audio/AudioCodec.h>
#include <iiFileProvider>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

class ManualExecutor final : public iiSharedCanvas::FrameRenderExecutor {
  public:
    void post(std::function<void()> task) override {tasks.push_back(std::move(task));}
    std::size_t maxConcurrency() const noexcept override {return 1;}
    std::vector<std::function<void()>> tasks;
};
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;using namespace iiSharedCanvas;
        ii::runtime::EventLoop loop;auto worker=std::make_shared<ManualExecutor>();
        auto document=native::DocumentSession::create();
        auto panel=editor::AudioPanelModel::create(document,loop,worker);panel->setVisible(true);
        panel->showMix();check(panel->mixMode.get() && !panel->importMode.get(),"audio mix tab did not switch");
        panel->showImport();check(panel->importMode.get() && !panel->mixMode.get(),"audio import tab did not switch");
        check(!panel->canCreate.get() && !panel->createTrack(),"audio tracks were created without a document");
        Document canvas;canvas.extent={4,4};canvas.timeline={{24,1},24};good(document->attach(canvas,"audio.iisc"));
        auto run=[&] {check(worker->tasks.size()==1,"expected one audio worker");auto task=std::move(worker->tasks.front());worker->tasks.clear();task();(void)loop.poll();};
        good(panel->createTrack());check(panel->busy.get() && document->document()->audioTracks.empty(),"track creation ran on the view thread");
        run();check(!panel->busy.get() && panel->trackNames.get().size()==2 && panel->selectedTrack.get()==1 &&
                    document->document()->audioTracks.size()==1,"created audio track was not selected");
        const auto original=document->snapshot();
        good(panel->setMutedDraft(true));good(panel->editGain("-6"));
        check(document->snapshot()==original && panel->canApply.get(),"track draft changed the document before Apply");
        good(panel->applyTrack());run();
        check(document->document()->audioTracks.front().muted && document->document()->audioTracks.front().gainDb==-6,
              "track settings did not reach the IISC document");
        good(panel->editGain("-12"));good(document->editPublication("title","Unrelated metadata"));
        check(panel->gainText.get()=="-12" && panel->canApply.get(),"an unrelated document edit discarded the track draft");
        good(panel->resetTrack());check(panel->gainText.get()=="-6","track reset did not restore the actual gain");
        good(panel->setGainDraft(-3));check(panel->gainText.get()=="-3" && panel->gainValue.get()==-3,
            "the gain slider did not update the draft");good(panel->resetTrack());
        good(panel->editGain("invalid"));check(!panel->canApply.get() && !panel->applyTrack(),"invalid gain was applied");
        good(panel->editGain("-6"));
        iiFileProvider::native::TemporaryDirectory fixture(std::filesystem::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(fixture.isValid(),"cannot create WAV fixture");const auto file=fixture.path()/"voice.wav";
        AudioAsset wave{"source",48000,1,std::vector<std::int16_t>(48001,1234)};
        const auto encoded=encodeAudioWav(wave);check(encoded.ok(),"WAV fixture encoding failed");
        iiFileProvider::native::File::write(file,std::string_view(reinterpret_cast<const char *>(encoded.bytes.data()),encoded.bytes.size()));
        bool picked=false;panel->setPickHandler([&]() -> ii::core::Result<void> {picked=true;return {};});
        good(panel->chooseFile());check(picked,"audio chooser was not invoked");
        const auto beforeImport=document->snapshot();good(panel->importFile(file));
        check(panel->busy.get() && document->snapshot()==beforeImport,"WAV decoding ran on the view thread");
        run();check(document->document()->audioTracks.front().clips.size()==1 && panel->summary.get().find("Voice")==std::string::npos &&
                    panel->summary.get().find("voice.wav")!=std::string::npos,
                    "imported WAV was not shown on the selected real track");
        panel->showTrim();check(panel->trimMode.get() && !panel->mixMode.get(),"audio trim tab did not switch");
        check(panel->clipNames.get().size()==2,"trim did not list the selected track's actual clips");
        good(panel->editGain("-4"));good(panel->selectClip(1));
        check(panel->gainText.get()=="-4" && panel->canApply.get(),"clip selection discarded the unrelated mix draft");
        good(panel->resetTrack());const auto beforeTrim=document->snapshot();
        good(panel->editTrim("0.083333 - 0.75"));
        check(panel->canTrim.get() && document->snapshot()==beforeTrim,"trim draft modified the document");
        good(panel->editGain("-5"));good(panel->resetTrack());
        check(panel->trimRange.get()=="0.083333 - 0.75" && panel->canTrim.get(),
              "resetting the mix discarded an unrelated trim draft");
        good(document->editPublication("title","Metadata during trim"));
        check(panel->canTrim.get(),"unrelated metadata discarded a clip trim draft");
        good(panel->applyTrim());run();
        check(document->document()->audioTracks.front().clips.front().startFrame==2 &&
              document->document()->audioTracks.front().clips.front().durationFrames==16 &&
              document->document()->audioTracks.front().clips.front().sourceOffsetSamples==4000,
              "trim panel did not update the real clip range");
        good(panel->editTrim("not a range"));check(!panel->canTrim.get() && !panel->applyTrim(),
              "an invalid trim range was accepted");good(panel->resetTrim());
        good(panel->editTrim("00:00.125 — 00:00.70"));check(panel->canTrim.get(),"timecode trim syntax was rejected");
        good(panel->applyTrim());good(document->editPublication("title","Newer during trim"));run();
        check(document->document()->audioTracks.front().clips.front().startFrame==2 &&
              panel->trimRange.get()=="00:00.125 — 00:00.70" && panel->status.get().find("changed")!=std::string::npos,
              "a stale trim overwrote the newer document or discarded its draft");good(panel->resetTrim());
        const auto saved=native::createCanvasFile(fixture.path()/"audio.iisc",*document->document());
        check(bool(saved),"cannot save an audio canvas fixture");
        const auto reopened=native::openCanvasFile(fixture.path()/"audio.iisc");
        check(reopened && reopened->document.audioTracks.size()==1 && reopened->document.audioTracks.front().clips.size()==1 &&
              reopened->document.audioAssets.size()==1 && reopened->document.audioAssets.front().samples[48000]==1234 &&
              reopened->document.audioAssets.front().samples.back()==0,
              "audio samples and clip did not survive actual IISC save and reopen");
        auto first=document->snapshot();good(panel->importFile(file));good(document->editPublication("title","Concurrent edit"));run();
        check(document->document()->audioTracks.front().clips.size()==1 && document->snapshot()!=first &&
              panel->status.get().find("changed")!=std::string::npos,"a stale import replaced the newer document");
        // The timeline uses the same worker-backed audio editing path even when
        // its dedicated inspector is not the active panel.
        auto workspace=editor::WorkspaceModel::create(document);
        workspace->setVisible(true);workspace->setAudioPanel(panel);
        auto timeline=workspace->timelinePanel();
        auto projection=timeline->project({.frames={0,25},.top=0,.bottom=22});
        check(projection && projection->clips.size()==1 && !projection->clips[0].locked,
              "audio clip was not editable from the timeline");
        using ii::gui::EditorTimelineGesture;using ii::gui::EditorTimelinePhase;
        const auto beforeMove=document->snapshot();
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_clip,
                              .id="audioClip/0/0",.frame=3}));
        good(timeline->input({.phase=EditorTimelinePhase::update,.kind=EditorTimelineGesture::move_clip,
                              .id="audioClip/0/0",.frame=6}));
        projection=timeline->project({.frames={0,25},.top=0,.bottom=22});
        check(projection && projection->clips[0].first==5 && projection->clips[0].end==21 &&
              document->snapshot()==beforeMove,"audio drag did not preview without editing the document");
        good(timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::move_clip,
                              .id="audioClip/0/0",.frame=6}));
        check(document->snapshot()==beforeMove && worker->tasks.size()==1,
              "audio timeline edit did not run on the worker");
        run();
        const auto &moved=document->document()->audioTracks.front().clips.front();
        check(moved.startFrame==5 && moved.durationFrames==16 && moved.sourceOffsetSamples==4000 &&
              beforeMove->audioTracks.front().clips.front().startFrame==2,
              "timeline move did not preserve the audio samples and source snapshot");
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::trim_start,
                              .id="audioClip/0/0",.frame=5}));
        good(timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::trim_start,
                              .id="audioClip/0/0",.frame=6}));
        run();
        const auto &trimmed=document->document()->audioTracks.front().clips.front();
        check(trimmed.startFrame==6 && trimmed.durationFrames==15 && trimmed.sourceOffsetSamples==6000,
              "timeline trim did not preserve the clip's corresponding source time");
        const auto beforeCancel=document->snapshot();
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_clip,
                              .id="audioClip/0/0",.frame=7}));
        good(timeline->input({.phase=EditorTimelinePhase::update,.kind=EditorTimelineGesture::move_clip,
                              .id="audioClip/0/0",.frame=8}));
        good(timeline->input({.phase=EditorTimelinePhase::cancel,.kind=EditorTimelineGesture::move_clip,
                              .id="audioClip/0/0",.frame=8}));
        check(document->snapshot()==beforeCancel && worker->tasks.empty(),"canceled audio drag edited the canvas");
        good(timeline->input({.phase=EditorTimelinePhase::begin,.kind=EditorTimelineGesture::move_clip,
                              .id="audioClip/0/0",.frame=7}));
        good(timeline->input({.phase=EditorTimelinePhase::end,.kind=EditorTimelineGesture::move_clip,
                              .id="audioClip/0/0",.frame=8}));
        good(document->editPublication("title","Changed during audio move"));run();
        check(document->document()->audioTracks.front().clips.front().startFrame==6 &&
              timeline->status.get().find("changed")!=std::string::npos,
              "stale timeline audio work replaced a newer document or hid its error");
        check(bool(native::createCanvasFile(fixture.path()/"timeline-audio.iisc",*document->document())),
              "cannot save the timeline-edited canvas");
        const auto reopenedTimeline=native::openCanvasFile(fixture.path()/"timeline-audio.iisc");
        check(reopenedTimeline && reopenedTimeline->document.audioTracks.front().clips.front().startFrame==6 &&
              reopenedTimeline->document.audioTracks.front().clips.front().sourceOffsetSamples==6000,
              "timeline audio edits did not survive saving and reopening the IISC file");
        workspace->setAudioPanel({});
        panel->close();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
