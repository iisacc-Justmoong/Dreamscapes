#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iiSDKCore/Runtime/Query.h>

namespace dreamscapes::vm {
enum class PlaybackAction { start, tick, stop, seek };
struct PlaybackRequest final {
    PlaybackAction action;
    std::chrono::steady_clock::time_point now{};
    double frame = 0, fps = 24;
    int totalFrames = 1, snapStep = 1;
    bool loop = false;
};
struct PlaybackReply final { bool accepted = false, playing = false; int frame = 0; };
// One clock per timeline activation. No timer/thread or document is owned here.
// The GUI adapter supplies wakeups, while elapsed time and frame decisions are
// always evaluated by this compiled, typed iiSDK receiver.
class TimelinePlayback final {
  public:
    TimelinePlayback() : commands_([this](const PlaybackRequest &request) { return execute(request); }) {}
    const ii::runtime::QueryPort<PlaybackRequest, PlaybackReply> &commands() const { return commands_; }
  private:
    PlaybackReply execute(const PlaybackRequest &request) {
        if (request.action == PlaybackAction::stop) {
            playing_ = false; return {true, false, frame_};
        }
        if (!std::isfinite(request.frame) || !std::isfinite(request.fps) || request.fps < 1 ||
            request.fps > 1000 || request.totalFrames < 1) return {false, playing_, frame_};
        const auto bounded = [&](double frame) {
            return static_cast<int>(std::clamp(std::floor(frame + .5), 0., double(request.totalFrames - 1)));
        };
        if (request.action == PlaybackAction::seek) {
            playing_ = false;
            const double step = std::max(1, request.snapStep);
            frame_ = bounded(std::round(request.frame / step) * step);
            return {true, false, frame_};
        }
        if (request.action == PlaybackAction::start ||
            (playing_ && (fps_ != request.fps || total_ != request.totalFrames))) {
            playing_ = true; anchor_ = request.now; first_ = bounded(request.frame);
            fps_ = request.fps; total_ = request.totalFrames; frame_ = first_;
            return {true, true, frame_};
        }
        if (!playing_) return {true, false, frame_};
        const double elapsed = std::max(0., std::chrono::duration<double>(request.now - anchor_).count());
        const double next = first_ + std::floor(elapsed * fps_);
        if (next >= total_) {
            if (request.loop) frame_ = static_cast<int>(std::fmod(next, double(total_)));
            else { playing_ = false; frame_ = 0; }
        } else frame_ = static_cast<int>(next);
        return {true, playing_, frame_};
    }
    bool playing_ = false;
    int frame_ = 0, first_ = 0, total_ = 1;
    double fps_ = 24;
    std::chrono::steady_clock::time_point anchor_{};
    ii::runtime::QueryPort<PlaybackRequest, PlaybackReply> commands_;
};
} // namespace dreamscapes::vm
