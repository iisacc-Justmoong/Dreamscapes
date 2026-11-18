#include "NativeTimelineAudio.h"
#include <iiDeviceMultimedia>
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#if defined(_WIN32)
#include <objbase.h>
#endif

namespace dreamscapes::editor {
using ii::core::ErrorCode;
using namespace iiSharedCanvas;
namespace {
constexpr std::size_t channels=2,blockFrames=1024;
constexpr auto waitInterval=std::chrono::milliseconds(5);
std::uint64_t clampFrame(long double value,std::uint64_t maximum) {
    if (value<=0) return 0;
    if (value>=static_cast<long double>(maximum)) return maximum;
    return static_cast<std::uint64_t>(std::ceil(value));
}
class HardwareOutput final : public TimelineAudioOutput {
  public:
    ii::core::Result<void> open() override {
        if (!ii::device::hardware_capabilities().audio)
            return ii::core::fail(ErrorCode::unsupported,"Audio output is unavailable on this platform");
        try {
            hardware_=std::make_unique<ii::device::Hardware>([](auto){});
            auto endpoint=hardware_->open_audio(ii::device::DeviceKind::audio_output,{},
                ii::device::AudioConfig{double(timelineAudioRate),channels,128,512});
            if (!endpoint) return ii::core::fail(ErrorCode::failed,"Cannot open the audio output device");
            endpoint_=*endpoint;return {};
        } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot open the audio output device",std::current_exception());}
    }
    ii::core::Result<std::size_t> write(std::span<const float> samples) override {
        if (!hardware_ || !endpoint_) return ii::core::fail(ErrorCode::closed,"Audio output is closed");
        const auto result=hardware_->audio_write(*endpoint_,samples);
        if (!result) {
            if (result.error().code==ii::device::ErrorCode::would_block) return std::size_t{0};
            return ii::core::fail(ErrorCode::failed,"The audio output device stopped");
        }
        return *result;
    }
    void close() noexcept override {
        if (hardware_ && endpoint_) (void)hardware_->close(*endpoint_);
        endpoint_.reset();if (hardware_) (void)hardware_->close();hardware_.reset();
    }
    ~HardwareOutput() override {close();}
  private:
    std::unique_ptr<ii::device::Hardware> hardware_;
    std::optional<ii::device::Endpoint> endpoint_;
};
}
ii::core::Result<TimelineAudioMixer> TimelineAudioMixer::create(std::shared_ptr<const Document> document,FrameIndex first) {
    if (!document || first>=document->timeline.frameCount)
        return ii::core::fail(ErrorCode::invalid_argument,"Choose a frame inside the audio timeline");
    const auto rate=document->timeline.frameRate;
    const auto frames=audioSampleFrameCount(document->timeline.frameCount-first,rate,timelineAudioRate);
    if (!frames || !rate.numerator || !rate.denominator)
        return ii::core::fail(ErrorCode::invalid_argument,"The audio timeline rate is invalid");
    TimelineAudioMixer mixer;mixer.document_=std::move(document);mixer.frameCount_=*frames;
    const auto secondsPerFrame=static_cast<long double>(rate.denominator)/rate.numerator;
    mixer.startSeconds_=first*secondsPerFrame;
    for (const auto &track:mixer.document_->audioTracks) if (!track.muted) for (const auto &clip:track.clips) if (clip.enabled) {
        if (!std::isfinite(track.gainDb) || !std::isfinite(clip.gainDb))
            return ii::core::fail(ErrorCode::invalid_argument,"The audio track gain is invalid");
        const auto *asset=findAudioAsset(*mixer.document_,clip.assetId);
        if (!asset || !asset->sampleRate || !asset->channelCount || asset->channelCount>2 ||
            asset->samples.empty() || asset->samples.size()%asset->channelCount) continue;
        const auto start=static_cast<long double>(clip.startFrame)*secondsPerFrame;
        const auto end=(static_cast<long double>(clip.startFrame)+clip.durationFrames)*secondsPerFrame;
        const auto begin=clampFrame((start-mixer.startSeconds_)*timelineAudioRate,*frames);
        const auto finish=clampFrame((end-mixer.startSeconds_)*timelineAudioRate,*frames);
        const auto gain=std::pow(10.0L,(track.gainDb+clip.gainDb)/20.0L);
        if (!std::isfinite(gain)) return ii::core::fail(ErrorCode::invalid_argument,"The audio track gain is invalid");
        if (begin<finish) mixer.spans_.push_back({asset,begin,finish,clip.sourceOffsetSamples,start,gain});
    }
    return mixer;
}
ii::core::Result<void> TimelineAudioMixer::mix(std::uint64_t first,std::span<float> output) const {
    if (output.size()%channels || output.size()>8192*channels || first>frameCount_ || output.size()/channels>frameCount_-first)
        return ii::core::fail(ErrorCode::out_of_range,"Invalid audio mix block");
    std::vector<long double> mixed(output.size(),0);
    const auto end=first+output.size()/channels;
    for (const auto &span:spans_) {
        const auto begin=std::max(first,span.begin),finish=std::min(end,span.end);
        const auto sourceCount=span.asset->samples.size()/span.asset->channelCount;
        for (auto index=begin;index<finish;++index) {
            const auto time=startSeconds_+static_cast<long double>(index)/timelineAudioRate;
            const auto position=(time-span.clipStart)*span.asset->sampleRate+span.sourceOffset;
            if (position<0 || position>=sourceCount) continue;
            const auto left=static_cast<std::size_t>(position),right=std::min(left+1,sourceCount-1);
            const auto fraction=position-left;
            for (std::size_t channel=0;channel<channels;++channel) {
                const auto c=std::min<std::size_t>(channel,span.asset->channelCount-1);
                const auto a=span.asset->samples[left*span.asset->channelCount+c];
                const auto b=span.asset->samples[right*span.asset->channelCount+c];
                mixed[(index-first)*channels+channel]+=(a*(1-fraction)+b*fraction)*span.gain/32768.0L;
            }
        }
    }
    std::ranges::transform(mixed,output.begin(),[](long double sample){return float(std::clamp(sample,-1.0L,1.0L));});
    return {};
}
ii::core::Result<void> streamTimelineAudio(const TimelineAudioRequest &request,std::stop_token stop,
    TimelineAudioOutputFactory factory,TimelineAudioNow now,TimelineAudioWait wait) {
    auto mixer=TimelineAudioMixer::create(request.document,request.firstFrame);
    if (!mixer) return std::unexpected(mixer.error());
    if (!mixer->hasContent() || stop.stop_requested()) return {};
    if (!factory) factory=[] {return std::make_unique<HardwareOutput>();};
    if (!now) now=[] {return ii::runtime::Clock::now();};
    if (!wait) wait=[](auto delay) {std::this_thread::sleep_for(delay);};
#if defined(_WIN32)
    const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if (FAILED(initialized)) return ii::core::fail(ErrorCode::failed,"Audio output requires a multithreaded Windows worker");
    struct ComScope {~ComScope(){CoUninitialize();}} com;
#endif
    auto output=factory();if (!output) return ii::core::fail(ErrorCode::failed,"Audio output is unavailable");
    if (auto opened=output->open();!opened) return opened;
    struct CloseOutput {TimelineAudioOutput *output;~CloseOutput(){output->close();}} closer{output.get()};
    const auto elapsed=std::max(0.0L,std::chrono::duration<long double>(now()-request.origin).count());
    const auto elapsedFrames=elapsed*timelineAudioRate;
    auto offset=elapsedFrames>=mixer->frameCount()?mixer->frameCount():static_cast<std::uint64_t>(elapsedFrames);
    std::vector<float> buffer(blockFrames*channels);
    while (offset<mixer->frameCount() && !stop.stop_requested()) {
        const auto frames=std::min<std::uint64_t>(blockFrames,mixer->frameCount()-offset);
        auto samples=std::span(buffer).first(static_cast<std::size_t>(frames)*channels);
        if (auto mixed=mixer->mix(offset,samples);!mixed) return mixed;
        std::size_t written=0;
        while (written<samples.size() && !stop.stop_requested()) {
            auto count=output->write(samples.subspan(written));
            if (!count) return std::unexpected(count.error());
            if (*count>samples.size()-written || *count%channels)
                return ii::core::fail(ErrorCode::failed,"Audio output returned an invalid sample count");
            if (!*count) wait(waitInterval);else written+=*count;
        }
        offset+=frames;
    }
    const auto duration=static_cast<long double>(mixer->frameCount())/timelineAudioRate;
    while (!stop.stop_requested() && std::chrono::duration<long double>(now()-request.origin).count()<duration)
        wait(waitInterval);
    return {};
}
struct TimelineAudio::Bridge final {
    struct Completion {std::uint64_t id;std::string error;};
    std::mutex mutex;std::optional<Completion> completion;
    ii::runtime::EventLoop::Sender sender;ii::runtime::TaskToken wake;bool closed=false;
};
TimelineAudio::TimelineAudio(std::shared_ptr<native::DocumentSession> document,std::shared_ptr<TimelinePlayback> playback,
    ii::runtime::EventLoop::Sender sender,Submit submit,TimelineAudioRunner runner)
    :document_(std::move(document)),playback_(std::move(playback)),sender_(std::move(sender)),
     submit_(std::move(submit)),runner_(std::move(runner)) {
    if (!document_ || !playback_ || !submit_) throw std::invalid_argument("Timeline audio requires a document, playback and worker");
    if (!runner_) runner_=[](const auto &request,std::stop_token stop) {return streamTimelineAudio(request,stop);};
}
std::shared_ptr<TimelineAudio> TimelineAudio::create(std::shared_ptr<native::DocumentSession> document,
    std::shared_ptr<TimelinePlayback> playback,ii::runtime::EventLoop &loop,Submit submit,TimelineAudioRunner runner) {
    auto value=std::shared_ptr<TimelineAudio>(new TimelineAudio(std::move(document),std::move(playback),loop.sender(),std::move(submit),std::move(runner)));
    value->start();return value;
}
void TimelineAudio::start() {
    const std::weak_ptr<TimelineAudio> weak=shared_from_this();
    const auto registered=sender_.on_signal([weak]{if (auto self=weak.lock()) self->deliver();});
    if (!registered) throw std::runtime_error("Cannot register timeline audio completion");
    wake_=registered.token;bridge_=std::make_shared<Bridge>();bridge_->sender=sender_;bridge_->wake=wake_;
    playing_=playback_->playing.observe([weak](bool value,std::uint64_t){if (auto self=weak.lock()) self->playbackChanged(value);},true);
}
TimelineAudio::~TimelineAudio() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
void TimelineAudio::playbackChanged(bool playing) {
    if (!active_ || owner_!=std::this_thread::get_id()) return;
    if (stop_) stop_->request_stop();stop_.reset();const auto id=++request_;
    if (!playing) return;
    (void)status.set({});
    auto document=document_->snapshot();
    if (!document || std::ranges::none_of(document->audioTracks,[](const auto &track) {
            return !track.muted && std::ranges::any_of(track.clips,[](const auto &clip){return clip.enabled;});
        })) return;
    TimelineAudioRequest request{std::move(document),static_cast<FrameIndex>(document_->frame.get()),playback_->origin()};
    stop_=std::make_shared<std::stop_source>();auto stop=stop_;auto bridge=bridge_;auto runner=runner_;
    bool accepted=false;
    try {accepted=submit_([request=std::move(request),stop,bridge,runner=std::move(runner),id] {
        if (stop->stop_requested()) return;
        std::string error;
        try {auto result=runner(request,stop->get_token());if (!result) error=result.error().message;}
        catch (...) {error="Audio playback failed";}
        if (stop->stop_requested()) return;
        {std::lock_guard lock(bridge->mutex);if (bridge->closed) return;bridge->completion=Bridge::Completion{id,std::move(error)};}
        (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
    });} catch (...) {accepted=false;}
    if (!accepted) (void)status.set("Audio playback worker is unavailable");
}
void TimelineAudio::deliver() {
    if (!active_ || owner_!=std::this_thread::get_id()) return;
    std::optional<Bridge::Completion> completion;
    {std::lock_guard lock(bridge_->mutex);completion=std::exchange(bridge_->completion,{});}
    if (completion && completion->id==request_) (void)status.set(std::move(completion->error));
}
void TimelineAudio::close() {
    if (!active_ || owner_!=std::this_thread::get_id()) return;
    active_=false;playing_={};if (stop_) stop_->request_stop();
    {std::lock_guard lock(bridge_->mutex);bridge_->closed=true;bridge_->completion.reset();}
    (void)sender_.cancel(wake_);submit_={};runner_={};
}
}
