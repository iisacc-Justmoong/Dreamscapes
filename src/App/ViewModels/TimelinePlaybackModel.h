#pragma once
#include "Native/TimelinePlayback.h"
#include "VideoTimelineModel.h"
#include <QPointer>
#include <QTimer>

namespace dreamscapes {
class TimelinePlaybackModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(dreamscapes::VideoTimelineModel *timeline READ timeline WRITE setTimeline NOTIFY timelineChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loop READ loop WRITE setLoop NOTIFY loopChanged)
  public:
    explicit TimelinePlaybackModel(QObject *parent = nullptr);
    VideoTimelineModel *timeline() const { return timeline_; }
    void setTimeline(VideoTimelineModel *);
    bool playing() const { return playing_; }
    void setPlaying(bool);
    bool active() const { return active_; }
    void setActive(bool);
    bool loop() const { return loop_; }
    void setLoop(bool);
    Q_INVOKABLE void seekFrame(double frame, int step = 1);
  signals:
    void timelineChanged();
    void playingChanged();
    void activeChanged();
    void loopChanged();
  private:
    vm::PlaybackRequest request(vm::PlaybackAction) const;
    void tick();
    void adopt(const vm::PlaybackReply &, bool seek);
    QPointer<VideoTimelineModel> timeline_;
    QTimer timer_;
    vm::TimelinePlayback clock_;
    bool playing_ = false, active_ = true, loop_ = false;
};
} // namespace dreamscapes
