#include "NativeReviewQtPlayback.h"
#include <QImage>
#include <QThread>
#include <QTransform>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dreamscapes::review {
ReviewVideoFrames::ReviewVideoFrames(Converter convert,Delivery deliver,QObject *parent)
    : QObject(parent),convert_(std::move(convert)),deliver_(std::move(deliver)) {
    if (!convert_ || !deliver_) throw std::invalid_argument("Frame processing requires a converter and delivery");
    connect(&watcher_,&QFutureWatcher<Result>::finished,this,[this]{finished();});
}
void ReviewVideoFrames::submit(QVideoFrame frame,QSize maximum,std::uint64_t generation) {
    if (thread()!=QThread::currentThread()) throw std::logic_error("Frame processing requires its owner thread");
    Request request{std::move(frame),maximum,generation,serial_};
    if (busy_) {pending_=std::move(request); return;}
    start(std::move(request));
}
void ReviewVideoFrames::clear() {
    if (thread()!=QThread::currentThread()) throw std::logic_error("Frame processing requires its owner thread");
    ++serial_; pending_.reset();
}
void ReviewVideoFrames::start(Request request) {
    busy_=true; runningSerial_=request.serial; runningGeneration_=request.generation;
    try {
        auto convert=convert_;
        watcher_.setFuture(QtConcurrent::run([convert=std::move(convert),request=std::move(request)]() mutable -> Result {
            try {return convert(request.frame,request.maximum);}
            catch (...) {return ii::core::fail(ii::core::ErrorCode::failed,"Video frame conversion failed",std::current_exception());}
        }));
    } catch (...) {busy_=false; throw;}
}
void ReviewVideoFrames::finished() {
    busy_=false;
    auto result=watcher_.result();
    const auto generation=runningGeneration_,serial=runningSerial_;
    const QPointer<ReviewVideoFrames> self(this);
    if (serial==serial_) {auto deliver=deliver_; deliver(generation,std::move(result));}
    if (!self) return;
    if (!busy_ && pending_) {
        auto request=std::move(*pending_); pending_.reset();
        const auto generation=request.generation;
        try {start(std::move(request));}
        catch (...) {
            auto deliver=deliver_;
            deliver(generation,ii::core::fail(ii::core::ErrorCode::failed,"Video frame scheduling failed",std::current_exception()));
        }
    }
}
QtReviewPlayback::QtReviewPlayback(double pixelScale,QObject *parent)
    : QObject(parent),frames_(raster,[this](std::uint64_t generation,ReviewVideoFrames::Result image) {
        if (!armed_ || !framesReady_ || generation_!=generation || player_.source()!=source_) return;
        if (!image) {auto callback=events_.error; if (callback) callback(generation,image.error().message); return;}
        auto callback=events_.frame; if (callback) callback(generation,std::move(*image));
    }),pixelScale_(pixelScale) {
    if (!std::isfinite(pixelScale_) || pixelScale_<=0 || pixelScale_>16) throw std::invalid_argument("Invalid video pixel scale");
    player_.setAudioOutput(&audio_); player_.setVideoSink(&sink_);
}
QtReviewPlayback::~QtReviewPlayback() { disconnectSource(); player_.stop(); }
ii::core::Result<void> QtReviewPlayback::owner() const {
    if (thread()!=QThread::currentThread()) return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Video playback requires its owner thread");
    return {};
}
void QtReviewPlayback::setEvents(Events value) {
    if (!owner()) throw std::logic_error("Video playback requires its owner thread");
    events_=std::move(value);
}
void QtReviewPlayback::disconnectSource() {
    armed_=false; framesReady_=false;
    frames_.clear();
    for (const auto &connection : sourceConnections_) QObject::disconnect(connection);
    sourceConnections_.clear(); source_=QUrl{};
}
ii::core::Result<void> QtReviewPlayback::load(std::string source,std::uint64_t generation) {
    if (auto result=owner(); !result) return result;
    const auto url=QUrl(QString::fromUtf8(source.data(),qsizetype(source.size())),QUrl::StrictMode);
    if (!url.isValid() || url.isEmpty()) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Invalid video source URL");
    disconnectSource(); generation_=generation; source_=url;
    const QPointer<QtReviewPlayback> self(this);
    const auto state=[self,generation] {if (self) self->publishState(generation);};
    sourceConnections_.push_back(connect(&player_,&QMediaPlayer::playbackStateChanged,this,[state](auto){state();}));
    sourceConnections_.push_back(connect(&player_,&QMediaPlayer::positionChanged,this,[state](auto){state();}));
    sourceConnections_.push_back(connect(&player_,&QMediaPlayer::durationChanged,this,[state](auto){state();}));
    sourceConnections_.push_back(connect(&player_,&QMediaPlayer::mediaStatusChanged,this,[self,generation](auto status) {
        if (!self || !self->armed_ || self->generation_!=generation || self->player_.source()!=self->source_) return;
        if (status==QMediaPlayer::LoadedMedia || status==QMediaPlayer::BufferedMedia) self->framesReady_=true;
    }));
    sourceConnections_.push_back(connect(&player_,&QMediaPlayer::errorOccurred,this,[self,generation](auto,const QString &message) {
        if (!self || !self->armed_ || self->generation_!=generation || self->player_.source()!=self->source_) return;
        auto callback=self->events_.error;
        if (callback) callback(generation,message.isEmpty()?std::string("Video playback failed"):message.toStdString());
    }));
    sourceConnections_.push_back(connect(&sink_,&QVideoSink::videoFrameChanged,this,[self,generation](const QVideoFrame &frame) {
        if (!self || !self->armed_ || !self->framesReady_ || self->generation_!=generation || self->player_.source()!=self->source_) return;
        if (!frame.isValid()) return;
        try {self->frames_.submit(frame,self->maximum_,generation);}
        catch (...) {if (self) {auto callback=self->events_.error; if (callback) callback(generation,"Video frame scheduling failed");}}
    }));
    player_.setSource(url);
    if (!self) return ii::core::fail(ii::core::ErrorCode::closed,"Video playback retired while opening");
    armed_=true;
    const auto status=player_.mediaStatus();
    framesReady_=status==QMediaPlayer::LoadedMedia || status==QMediaPlayer::BufferedMedia;
    if (player_.error()!=QMediaPlayer::NoError) {
        auto callback=events_.error;
        if (callback) callback(generation,player_.errorString().isEmpty()?std::string("Video playback failed"):player_.errorString().toStdString());
        if (!self) return ii::core::fail(ii::core::ErrorCode::closed,"Video playback retired while reporting an error");
    }
    publishState(generation); return {};
}
void QtReviewPlayback::publishState(std::uint64_t generation) {
    if (!armed_ || generation_!=generation || player_.source()!=source_) return;
    const auto current=player_.playbackState();
    const auto state=current==QMediaPlayer::PlayingState?ReviewPlaybackState::playing:
        current==QMediaPlayer::PausedState?ReviewPlaybackState::paused:ReviewPlaybackState::stopped;
    auto callback=events_.state; if (callback) callback(generation,state,player_.position(),player_.duration());
}
ii::core::Result<void> QtReviewPlayback::play() {
    if (auto result=owner(); !result) return result;
    if (!armed_ || source_.isEmpty()) return ii::core::fail(ii::core::ErrorCode::closed,"No video source is loaded");
    player_.play(); return {};
}
ii::core::Result<void> QtReviewPlayback::pause() {
    if (auto result=owner(); !result) return result;
    if (!armed_ || source_.isEmpty()) return ii::core::fail(ii::core::ErrorCode::closed,"No video source is loaded");
    player_.pause(); return {};
}
ii::core::Result<void> QtReviewPlayback::replay() {
    if (auto result=owner(); !result) return result;
    if (!armed_ || source_.isEmpty()) return ii::core::fail(ii::core::ErrorCode::closed,"No video source is loaded");
    player_.setPosition(0); player_.play(); return {};
}
ii::core::Result<void> QtReviewPlayback::stop() {
    if (auto result=owner(); !result) return result;
    disconnectSource(); player_.stop(); player_.setSource({}); return {};
}
ii::core::Result<void> QtReviewPlayback::setViewport(double width,double height) {
    if (auto result=owner(); !result) return result;
    if (!std::isfinite(width) || !std::isfinite(height) || width<0 || height<0 || width>8192 || height>8192)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Invalid video viewport");
    maximum_=QSize(std::clamp(int(std::ceil(width*pixelScale_)),1,8192),
                   std::clamp(int(std::ceil(height*pixelScale_)),1,8192));
    return {};
}
ii::core::Result<std::shared_ptr<const ii::gui::RasterImage>> QtReviewPlayback::raster(const QVideoFrame &frame,QSize maximum) {
    if (!frame.isValid()) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Video frame is unavailable");
    auto image=frame.toImage();
    QTransform presentation;
    if (frame.rotation()!=QtVideo::Rotation::None) presentation.rotate(static_cast<int>(frame.rotation()));
    if (frame.mirrored()) presentation.scale(-1,1);
    if (!presentation.isIdentity()) image=image.transformed(presentation);
    if (maximum.isValid() && (image.width()>maximum.width() || image.height()>maximum.height()))
        image=image.scaled(maximum,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    image=image.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull() || image.width()<1 || image.height()<1 || image.width()>8192 || image.height()>8192)
        return ii::core::fail(ii::core::ErrorCode::unsupported,"Video frame cannot be converted to an RGBA image");
    const auto width=static_cast<std::size_t>(image.width()),height=static_cast<std::size_t>(image.height());
    if (height>std::numeric_limits<std::size_t>::max()/(width*4))
        return ii::core::fail(ii::core::ErrorCode::out_of_range,"Video frame is too large");
    std::vector<std::uint8_t> bytes(width*height*4);
    for (std::size_t row=0;row<height;++row) std::memcpy(bytes.data()+row*width*4,image.constScanLine(int(row)),width*4);
    return ii::gui::rgba_image(static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height),std::move(bytes),
                               {.maximum_pixels=static_cast<std::uint64_t>(width*height)});
}
} // namespace dreamscapes::review
