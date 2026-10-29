#include "TimelinePlaybackModel.h"
namespace dreamscapes {
TimelinePlaybackModel::TimelinePlaybackModel(QObject *parent) : QObject(parent), timer_(this) {
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &TimelinePlaybackModel::tick);
}
vm::PlaybackRequest TimelinePlaybackModel::request(vm::PlaybackAction action) const {
    return {action, std::chrono::steady_clock::now(), timeline_ ? double(timeline_->playhead()) : 0,
            timeline_ ? timeline_->fps() : 0, timeline_ ? timeline_->totalFrames() : 0, 1, loop_};
}
void TimelinePlaybackModel::setTimeline(VideoTimelineModel *timeline) {
    if (timeline_ == timeline) return;
    QPointer<TimelinePlaybackModel> self(this);
    setPlaying(false);
    if (!self) return;
    if (timeline_) disconnect(timeline_, nullptr, this, nullptr);
    timeline_ = timeline;
    if (timeline_) {
        connect(timeline_, &VideoTimelineModel::resetCompleted, this, [this] { setPlaying(false); });
        connect(timeline_, &QObject::destroyed, this, [this] {
            QPointer<TimelinePlaybackModel> self(this);
            setPlaying(false);
            if (self) emit timelineChanged();
        });
    }
    emit timelineChanged();
}
void TimelinePlaybackModel::adopt(const vm::PlaybackReply &reply, bool seek) {
    if (!reply.accepted) return;
    const bool changed = playing_ != reply.playing;
    playing_ = reply.playing;
    if (playing_ && timeline_) timer_.start(std::max(1, int(1000 / timeline_->fps())));
    else timer_.stop();
    QPointer<TimelinePlaybackModel> self(this);
    const auto timeline = timeline_;
    if (changed) emit playingChanged();
    if (self && seek && timeline && timeline->playhead() != reply.frame) timeline->seek(reply.frame);
}
void TimelinePlaybackModel::setPlaying(bool value) {
    value = value && active_ && timeline_;
    if (playing_ == value) return;
    const auto reply = clock_.commands().send(request(value ? vm::PlaybackAction::start : vm::PlaybackAction::stop));
    if (reply) adopt(*reply, false);
}
void TimelinePlaybackModel::setActive(bool value) {
    if (active_ == value) return;
    active_ = value;
    QPointer<TimelinePlaybackModel> self(this);
    if (!value) setPlaying(false);
    if (self) emit activeChanged();
}
void TimelinePlaybackModel::setLoop(bool value) {
    if (loop_ == value) return;
    loop_ = value; emit loopChanged();
}
void TimelinePlaybackModel::seekFrame(double frame, int step) {
    auto input = request(vm::PlaybackAction::seek);
    input.frame = frame; input.snapStep = timeline_ && timeline_->snap() ? step : 1;
    const auto reply = clock_.commands().send(input);
    if (reply) adopt(*reply, true);
}
void TimelinePlaybackModel::tick() {
    if (!active_ || !timeline_) { setPlaying(false); return; }
    const auto reply = clock_.commands().send(request(vm::PlaybackAction::tick));
    if (reply) adopt(*reply, true);
}
} // namespace dreamscapes
