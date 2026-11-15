#pragma once
#include "NativeReviewMedia.h"
#include <QAudioOutput>
#include <QFutureWatcher>
#include <QMediaPlayer>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <functional>
#include <optional>
#include <vector>

namespace dreamscapes::review {
// Converts at most one frame at a time and keeps only the newest waiting frame.
// Results from a retired source never reach its replacement.
class ReviewVideoFrames final : public QObject {
  public:
    using Image=std::shared_ptr<const ii::gui::RasterImage>;
    using Result=ii::core::Result<Image>;
    using Converter=std::function<Result(const QVideoFrame &,QSize)>;
    using Delivery=std::function<void(std::uint64_t,Result)>;
    ReviewVideoFrames(Converter,Delivery,QObject *parent=nullptr);
    void submit(QVideoFrame,QSize,std::uint64_t);
    void clear();
  private:
    struct Request {QVideoFrame frame; QSize maximum; std::uint64_t generation,serial;};
    void start(Request);
    void finished();
    Converter convert_; Delivery deliver_;
    QFutureWatcher<Result> watcher_;
    std::optional<Request> pending_;
    std::uint64_t serial_=0,runningSerial_=0,runningGeneration_=0;
    bool busy_=false;
};
// Nonvisual transition backend. The LVUI view only depends on ReviewPlaybackPort.
class QtReviewPlayback final : public QObject,public ReviewPlaybackPort {
  public:
    explicit QtReviewPlayback(double pixelScale=1,QObject *parent=nullptr);
    ~QtReviewPlayback() override;
    void setEvents(Events) override;
    [[nodiscard]] ii::core::Result<void> load(std::string,std::uint64_t) override;
    [[nodiscard]] ii::core::Result<void> play() override;
    [[nodiscard]] ii::core::Result<void> pause() override;
    [[nodiscard]] ii::core::Result<void> replay() override;
    [[nodiscard]] ii::core::Result<void> stop() override;
    [[nodiscard]] ii::core::Result<void> setViewport(double width,double height) override;
    [[nodiscard]] static ii::core::Result<std::shared_ptr<const ii::gui::RasterImage>> raster(const QVideoFrame &,QSize maximum={});
  private:
    [[nodiscard]] ii::core::Result<void> owner() const;
    void disconnectSource();
    void publishState(std::uint64_t);
    QMediaPlayer player_;
    QAudioOutput audio_;
    QVideoSink sink_;
    ReviewVideoFrames frames_;
    Events events_;
    std::vector<QMetaObject::Connection> sourceConnections_;
    QUrl source_;
    std::uint64_t generation_=0;
    double pixelScale_=1;
    QSize maximum_;
    bool armed_=false,framesReady_=false;
};
} // namespace dreamscapes::review
