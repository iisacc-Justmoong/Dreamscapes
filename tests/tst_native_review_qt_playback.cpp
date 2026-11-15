#include "App/Views/Result/NativeReviewQtPlayback.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QThread>
#include <QTransform>
#include <QVideoFrame>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value,const char *message) { if (!value) throw std::runtime_error(message); }
template<class Predicate> void await(Predicate predicate) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed()<5000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    check(predicate(),"timed out while waiting for frame processing");
}
void boundedFrames() {
    using namespace dreamscapes::review;
    struct State {std::mutex mutex; std::condition_variable ready; bool released=false; std::atomic<int> started=0;};
    auto state=std::make_shared<State>();
    struct ReleaseOnExit {std::shared_ptr<State> state;
        ~ReleaseOnExit() {{std::lock_guard lock(state->mutex); state->released=true;} state->ready.notify_all();}} release{state};
    std::vector<std::uint64_t> delivered;
    ReviewVideoFrames pump(
        [state](const QVideoFrame &frame,QSize) -> ii::core::Result<std::shared_ptr<const ii::gui::RasterImage>> {
            ++state->started;
            if (frame.startTime()==1) {std::unique_lock lock(state->mutex); state->ready.wait(lock,[&]{return state->released;});}
            return ii::gui::rgba_image(1,1,{std::uint8_t(frame.startTime()),0,0,255});
        },
        [&](std::uint64_t generation,auto result) {check(bool(result),"fixture conversion failed"); delivered.push_back(generation);});
    const auto frame=[](int number) {QVideoFrame value(QImage(2,2,QImage::Format_RGBA8888)); value.setStartTime(number); return value;};
    pump.submit(frame(1),QSize(2,2),1); await([&]{return state->started.load()==1;});
    pump.submit(frame(2),QSize(2,2),2); pump.submit(frame(3),QSize(2,2),3);
    {std::lock_guard lock(state->mutex); state->released=true;} state->ready.notify_one();
    await([&]{return delivered.size()==2;});
    check(state->started==2 && delivered==std::vector<std::uint64_t>({1,3}),"frame queue failed to coalesce the latest frame");
    delivered.clear();
    pump.submit(frame(1),QSize(2,2),4); pump.clear();
    pump.submit(frame(3),QSize(2,2),5);
    await([&]{return delivered.size()==1;});
    check(delivered==std::vector<std::uint64_t>({5}),"retired frame was delivered after a source change");

    bool failed=false,recovered=false;
    ReviewVideoFrames errors(
        [](const QVideoFrame &value,QSize) -> ii::core::Result<std::shared_ptr<const ii::gui::RasterImage>> {
            if (value.startTime()==1) throw std::runtime_error("fixture conversion exception");
            return ii::gui::rgba_image(1,1,{1,2,3,255});
        },
        [&](std::uint64_t generation,auto result) {if (generation==6) failed=!result; else if (generation==7) recovered=bool(result);});
    errors.submit(frame(1),QSize(2,2),6); await([&]{return failed;});
    errors.submit(frame(3),QSize(2,2),7); await([&]{return recovered;});
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    try {
        QImage image(2,1,QImage::Format_RGBA8888);
        image.setPixelColor(0,0,QColor(255,30,10,255));
        image.setPixelColor(1,0,QColor(2,3,4,127));
        QVideoFrame frame(image);
        const auto expected=frame.toImage().convertToFormat(QImage::Format_RGBA8888);
        auto result=dreamscapes::review::QtReviewPlayback::raster(frame);
        if (!result || (*result)->width()!=2 || (*result)->height()!=1) throw std::runtime_error("video frame dimensions were lost");
        const auto rgba=(*result)->rgba();
        if (expected.isNull() || rgba.size()!=8 || rgba[3]!=255 || rgba[7]!=127 ||
            !std::equal(rgba.begin(),rgba.end(),expected.constScanLine(0)))
            throw std::runtime_error("video frame conversion changed decoded color or alpha");
        auto scaled=dreamscapes::review::QtReviewPlayback::raster(frame,QSize(1,1));
        if (!scaled || (*scaled)->width()!=1 || (*scaled)->height()!=1)
            throw std::runtime_error("video frame did not fit the viewport budget");
        frame.setRotation(QtVideo::Rotation::Clockwise90); frame.setMirrored(true);
        QTransform transform; transform.rotate(90); transform.scale(-1,1);
        const auto transformed=frame.toImage().transformed(transform).convertToFormat(QImage::Format_RGBA8888);
        auto rotated=dreamscapes::review::QtReviewPlayback::raster(frame);
        check(bool(rotated) && (*rotated)->width()==std::uint32_t(transformed.width()) &&
              (*rotated)->height()==std::uint32_t(transformed.height()),"video presentation orientation was lost");
        const auto pixels=(*rotated)->rgba();
        for (int y=0;y<transformed.height();++y)
            check(std::equal(pixels.begin()+y*transformed.width()*4,pixels.begin()+(y+1)*transformed.width()*4,
                             transformed.constScanLine(y)),"video presentation transform changed pixels");
        if (dreamscapes::review::QtReviewPlayback::raster(QVideoFrame{})) throw std::runtime_error("empty video frame was accepted");
        boundedFrames();
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
