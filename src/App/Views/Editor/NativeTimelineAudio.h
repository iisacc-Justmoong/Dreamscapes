#pragma once
#include "NativeTimelinePlayback.h"
#include <iiSDKCore>
#include <chrono>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

namespace dreamscapes::editor {
inline constexpr std::uint32_t timelineAudioRate=48000;
struct TimelineAudioRequest final {
    std::shared_ptr<const iiSharedCanvas::Document> document;
    iiSharedCanvas::FrameIndex firstFrame=0;
    ii::runtime::Clock::time_point origin;
};

// A bounded mixer borrows one immutable IISC snapshot for its lifetime.
class TimelineAudioMixer final {
  public:
    [[nodiscard]] static ii::core::Result<TimelineAudioMixer> create(std::shared_ptr<const iiSharedCanvas::Document>,
                                                                    iiSharedCanvas::FrameIndex);
    [[nodiscard]] ii::core::Result<void> mix(std::uint64_t firstSampleFrame,std::span<float> interleavedStereo) const;
    [[nodiscard]] std::uint64_t frameCount() const noexcept {return frameCount_;}
    [[nodiscard]] bool hasContent() const noexcept {return !spans_.empty();}
  private:
    struct Span final {
        const iiSharedCanvas::AudioAsset *asset=nullptr;
        std::uint64_t begin=0,end=0,sourceOffset=0;
        long double clipStart=0,gain=1;
    };
    std::shared_ptr<const iiSharedCanvas::Document> document_;
    std::vector<Span> spans_;
    std::uint64_t frameCount_=0;
    long double startSeconds_=0;
};

class TimelineAudioOutput {
  public:
    virtual ~TimelineAudioOutput()=default;
    [[nodiscard]] virtual ii::core::Result<void> open()=0;
    // Returns the number of interleaved samples accepted; zero means full.
    [[nodiscard]] virtual ii::core::Result<std::size_t> write(std::span<const float>)=0;
    virtual void close() noexcept=0;
};
using TimelineAudioOutputFactory=std::function<std::unique_ptr<TimelineAudioOutput>()>;
using TimelineAudioNow=std::function<ii::runtime::Clock::time_point()>;
using TimelineAudioWait=std::function<void(std::chrono::milliseconds)>;
using TimelineAudioRunner=std::function<ii::core::Result<void>(const TimelineAudioRequest &,std::stop_token)>;
[[nodiscard]] ii::core::Result<void> streamTimelineAudio(const TimelineAudioRequest &,std::stop_token,
    TimelineAudioOutputFactory={},TimelineAudioNow={},TimelineAudioWait={});

class TimelineAudio final : public std::enable_shared_from_this<TimelineAudio> {
  public:
    using Submit=std::function<bool(ii::runtime::Task)>;
    static std::shared_ptr<TimelineAudio> create(std::shared_ptr<native::DocumentSession>,std::shared_ptr<TimelinePlayback>,
        ii::runtime::EventLoop &,Submit,TimelineAudioRunner={});
    ~TimelineAudio();
    void close();
    ii::core::Observable<std::string> status;
  private:
    struct Bridge;
    TimelineAudio(std::shared_ptr<native::DocumentSession>,std::shared_ptr<TimelinePlayback>,
        ii::runtime::EventLoop::Sender,Submit,TimelineAudioRunner);
    void start();
    void playbackChanged(bool);
    void deliver();
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    std::shared_ptr<TimelinePlayback> playback_;
    ii::runtime::EventLoop::Sender sender_;
    ii::runtime::TaskToken wake_;
    Submit submit_;
    TimelineAudioRunner runner_;
    std::shared_ptr<Bridge> bridge_;
    std::shared_ptr<std::stop_source> stop_;
    ii::core::ObservableSubscription playing_;
    std::uint64_t request_=0;
    bool active_=true;
};
}
