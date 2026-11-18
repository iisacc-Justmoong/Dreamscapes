#include "App/Views/Editor/NativeTimelineAudio.h"
#include "App/Native/NativeApplicationShell.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition,const char *message) {if (!condition) throw std::runtime_error(message);}
void good(ii::core::Result<void> result) {if (!result) throw std::runtime_error(result.error().message);}
template<class T> T take(ii::core::Result<T> result) {if (!result) throw std::runtime_error(result.error().message);return std::move(*result);}
struct OutputState {std::vector<float> samples;bool opened=false,closed=false;};
class FakeOutput final : public dreamscapes::editor::TimelineAudioOutput {
  public:
    explicit FakeOutput(std::shared_ptr<OutputState> state):state_(std::move(state)) {}
    ii::core::Result<void> open() override {state_->opened=true;return {};}
    ii::core::Result<std::size_t> write(std::span<const float> values) override {
        const auto count=std::min<std::size_t>(128,values.size());
        state_->samples.insert(state_->samples.end(),values.begin(),values.begin()+count);return count;
    }
    void close() noexcept override {state_->closed=true;}
  private:std::shared_ptr<OutputState> state_;
};
class ManualExecutor final : public iiSharedCanvas::FrameRenderExecutor {
  public:void post(std::function<void()> task) override {tasks.push_back(std::move(task));}
    std::size_t maxConcurrency() const noexcept override {return 1;}
    std::vector<std::function<void()>> tasks;
};
}
int main() {
    try {
        using namespace dreamscapes;using namespace iiSharedCanvas;using namespace std::chrono_literals;
        Document canvas;canvas.extent={4,4};canvas.timeline={{24,1},24};
        canvas.audioAssets.push_back({"sound",24000,1,std::vector<std::int16_t>(24000,16384)});
        canvas.audioTracks.push_back({"track","Audio",false,0,{{"clip","Clip","sound",0,24,0,0,true}}});
        canvas.audioTracks.push_back({"muted","Muted",true,0,{{"other","Muted","sound",0,24,0,0,true}}});
        auto snapshot=std::make_shared<const Document>(canvas);
        auto mixer=take(editor::TimelineAudioMixer::create(snapshot,0));
        check(mixer.frameCount()==48000,"timeline audio duration did not follow its frame rate");
        std::vector<float> block(256);good(mixer.mix(0,block));
        check(std::ranges::all_of(block,[](float value){return std::abs(value-.5f)<.0001f;}),
              "mono samples, resampling or mute were not applied");
        auto adjusted=canvas;adjusted.audioTracks.front().gainDb=-6.020599913279624;
        adjusted.audioTracks.front().clips.front().sourceOffsetSamples=1;
        adjusted.audioAssets.front().samples[0]=0;
        auto offset=take(editor::TimelineAudioMixer::create(std::make_shared<const Document>(adjusted),0));
        good(offset.mix(0,block));
        check(std::abs(block.front()-.25f)<.0001f,"source sample offset was not applied");
        auto later=take(editor::TimelineAudioMixer::create(std::make_shared<const Document>(adjusted),1));
        good(later.mix(0,block));
        check(later.frameCount()==46000 && std::abs(block.front()-.25f)<.0001f,
              "seek position, source offset or track gain was not reflected in audio");
        auto output=std::make_shared<OutputState>();auto now=ii::runtime::Clock::now();
        const auto origin=now;
        good(editor::streamTimelineAudio({snapshot,0,origin},{},[output]{return std::make_unique<FakeOutput>(output);},
             [&]{return now;},[&](auto delay){now+=delay;}));
        check(output->opened && output->closed && output->samples.size()==96000 &&
              std::abs(output->samples.front()-.5f)<.0001f,"stream did not deliver partial writes to the output");
        ii::runtime::EventLoop loop;auto executor=std::make_shared<ManualExecutor>();
        auto shell=native::NativeApplicationShell::create("<svg xmlns=\"http://www.w3.org/2000/svg\"/>");
        good(shell->connectFrameRenderer(loop,executor));
        std::vector<ii::runtime::Task> jobs;unsigned played=0;
        good(shell->connectTimelineAudio(loop,[&](ii::runtime::Task task){jobs.push_back(std::move(task));return true;},
            [&](const editor::TimelineAudioRequest &request,std::stop_token stop) -> ii::core::Result<void> {
                check(!stop.stop_requested() && request.firstFrame==0 && request.document->audioTracks.size()==2,
                      "audio playback received the wrong canvas or position");++played;return {};
            }));
        auto shellCanvas=canvas;shellCanvas.timeline.frameCount=240;
        good(shell->documentSession()->attach(shellCanvas,"timeline.iisc"));good(shell->navigate("editor"));
        auto workspace=shell->editorWorkspace();const auto revision=shell->documentSession()->revision.get();
        workspace->togglePlayback();check(jobs.size()==1 && played==0,"timeline Play did not queue audio work");
        jobs.front()();jobs.clear();(void)loop.poll();
        check(played==1 && shell->documentSession()->revision.get()==revision && !shell->documentSession()->dirty.get(),
              "timeline playback changed the document or missed the audio runner");
        workspace->togglePlayback();workspace->togglePlayback();check(jobs.size()==1,"audio did not restart");
        workspace->togglePlayback();jobs.front()();jobs.clear();(void)loop.poll();
        check(played==1,"pause did not cancel pending audio");
        shell->disconnectTimelineAudio();
        good(shell->connectTimelineAudio(loop,[&](ii::runtime::Task task){jobs.push_back(std::move(task));return true;},
            [](const auto &,std::stop_token) -> ii::core::Result<void> {
                return ii::core::fail(ii::core::ErrorCode::failed,"Audio device unavailable");
            }));
        workspace->togglePlayback();check(jobs.size()==1,"audio error fixture was not started");
        jobs.front()();jobs.clear();(void)loop.poll();
        check(workspace->timelinePanel()->status.get()=="Audio device unavailable" && workspace->playText.get()=="Pause",
              "an audio device error was hidden or stopped visual playback");
        shell->disconnectTimelineAudio();shell->disconnectFrameRenderer();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
