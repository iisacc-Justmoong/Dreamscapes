#include "NativeHistogram.h"
#include <LVUI>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dreamscapes::editor {
namespace {
using ii::core::ErrorCode;
using Image=std::shared_ptr<const ii::gui::RasterImage>;
struct Analysis final {
    HistogramStatistics statistics;
    std::array<Image,4> plots;
    std::string summary,error;
};
std::array<std::uint8_t,3> bytes(lvui::StyleColor color) {
    const auto convert=[](double value){return static_cast<std::uint8_t>(std::lround(std::clamp(value,0.0,1.0)*255));};
    return {convert(color.red),convert(color.green),convert(color.blue)};
}
Analysis analyze(const ii::gui::RasterImage &source) {
    Analysis result;
    auto &stats=result.statistics;
    double sum=0,square=0;
    const auto pixels=source.rgba();
    for (std::size_t i=0;i+3<pixels.size();i+=4) {
        if (!pixels[i+3]) continue;
        const auto r=pixels[i],g=pixels[i+1],b=pixels[i+2];
        ++stats.red[r];++stats.green[g];++stats.blue[b];++stats.pixels;
        // Integer approximation of encoded Rec. 709 luminance, 0..255.
        const auto luminance=(54u*r+183u*g+19u*b+128u)>>8;
        sum+=luminance;square+=static_cast<double>(luminance)*luminance;
    }
    if (!stats.pixels) {result.summary="No visible preview pixels";return result;}
    stats.mean=sum/static_cast<double>(stats.pixels);
    stats.standardDeviation=std::sqrt(std::max(0.0,square/static_cast<double>(stats.pixels)-stats.mean*stats.mean));
    std::ostringstream text;
    text<<std::fixed<<std::setprecision(1)<<"Mean "<<stats.mean<<" · Std dev "<<stats.standardDeviation
        <<" · "<<stats.pixels<<" preview pixels";
    result.summary=text.str();
    constexpr std::size_t width=256,height=128;
    const std::array colors{bytes(lvui::figma_defaults::accent_red_default),
                            bytes(lvui::figma_defaults::accent_green_default),
                            bytes(lvui::figma_defaults::accent_blue_default)};
    const std::array<const std::array<std::uint64_t,256> *,3> bins{&stats.red,&stats.green,&stats.blue};
    for (std::size_t mode=0;mode<result.plots.size();++mode) {
        std::uint64_t maximum=1;
        for (std::size_t c=0;c<3;++c) if (!mode || mode==c+1)
            maximum=std::max(maximum,*std::max_element(bins[c]->begin(),bins[c]->end()));
        std::vector<std::uint8_t> rgba(width*height*4,0);
        for (std::size_t x=0;x<width;++x) for (std::size_t c=0;c<3;++c) {
            if (mode && mode!=c+1) continue;
            const auto count=(*bins[c])[x];
            const auto bar=count?std::max<std::size_t>(1,static_cast<std::size_t>(std::lround(
                static_cast<double>(count)*height/static_cast<double>(maximum)))):0;
            for (std::size_t y=height-bar;y<height;++y) {
                auto *pixel=rgba.data()+(y*width+x)*4;
                for (std::size_t component=0;component<3;++component)
                    pixel[component]=std::max(pixel[component],colors[c][component]);
                pixel[3]=255;
            }
        }
        auto image=ii::gui::rgba_image(width,height,std::move(rgba));
        if (!image) {result.error=image.error().message;return result;}
        result.plots[mode]=std::move(*image);
    }
    return result;
}
}
struct HistogramModel::Bridge final {
    struct Completion final {std::uint64_t version;Analysis analysis;};
    std::mutex mutex;
    std::optional<Completion> completed;
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    bool closed=false;
};
HistogramModel::HistogramModel(ii::runtime::EventLoop::Sender sender,
    std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor):sender_(std::move(sender)),executor_(std::move(executor)) {
    if (!executor_) throw std::invalid_argument("Histogram requires a worker");
}
std::shared_ptr<HistogramModel> HistogramModel::create(ii::runtime::EventLoop &loop,
    std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor) {
    auto model=std::shared_ptr<HistogramModel>(new HistogramModel(loop.sender(),std::move(executor)));
    model->start();return model;
}
void HistogramModel::start() {
    const std::weak_ptr<HistogramModel> weak=shared_from_this();
    auto admitted=sender_.on_signal([weak]{if (auto model=weak.lock()) model->deliver();});
    if (!admitted) throw std::runtime_error("Cannot register histogram completion");
    wake_=admitted.token;bridge_=std::make_shared<Bridge>();bridge_->sender=sender_;bridge_->wake=wake_;
}
HistogramModel::~HistogramModel() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
ii::core::Result<void> HistogramModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Histogram requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Histogram is closed");
    return {};
}
ii::core::Result<void> HistogramModel::setSource(Image source) {
    if (auto result=writable();!result) return result;
    if (source_==source) return {};
    source_=std::move(source);++version_;
    if (!source_) {
        plots_={};(void)plot.set({});(void)statistics.set({});(void)summary.set({});(void)status.set({});
        (void)busy.set(false);
    }
    if (visible_ && source_) {if (inflight_) (void)busy.set(true);schedule();}
    return {};
}
ii::core::Result<void> HistogramModel::setVisible(bool value) {
    if (auto result=writable();!result) return result;
    if (visible_==value) return {};
    visible_=value;++version_;
    if (visible_ && source_) {if (inflight_) (void)busy.set(true);schedule();}
    else if (!visible_) (void)busy.set(false);
    return {};
}
ii::core::Result<void> HistogramModel::select(HistogramChannel channel) {
    if (auto result=writable();!result) return result;
    const auto index=static_cast<std::size_t>(channel);
    if (index>=plots_.size()) return ii::core::fail(ErrorCode::invalid_argument,"Unknown histogram channel");
    channel_=channel;
    (void)rgbSelected.set(channel==HistogramChannel::rgb);(void)redSelected.set(channel==HistogramChannel::red);
    (void)greenSelected.set(channel==HistogramChannel::green);(void)blueSelected.set(channel==HistogramChannel::blue);
    (void)plot.set(plots_[index]);return {};
}
void HistogramModel::schedule() {
    if (!active_ || !visible_ || !source_ || inflight_) return;
    const auto version=version_;auto source=source_;auto bridge=bridge_;auto executor=executor_;
    inflight_=true;(void)busy.set(true);
    if (!active_ || !visible_ || version_!=version) {inflight_=false;schedule();return;}
    try {
        executor->post([source=std::move(source),bridge,version] {
            Analysis result;
            try {result=analyze(*source);} catch (const std::exception &error) {result.error=error.what();}
            catch (...) {result.error="Cannot analyze preview pixels";}
            {std::lock_guard lock(bridge->mutex);if (bridge->closed) return;
             bridge->completed.emplace(Bridge::Completion{version,std::move(result)});}
            (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
        });
    } catch (...) {
        inflight_=false;(void)busy.set(false);(void)status.set("Histogram worker is unavailable");
    }
}
void HistogramModel::deliver() {
    if (!active_) return;
    std::optional<Bridge::Completion> completion;
    {std::lock_guard lock(bridge_->mutex);completion=std::move(bridge_->completed);bridge_->completed.reset();}
    if (!completion) return;
    inflight_=false;
    if (visible_ && completion->version==version_) {
        auto result=std::move(completion->analysis);
        plots_=std::move(result.plots);
        (void)statistics.set(std::move(result.statistics));
        if (!active_ || completion->version!=version_) {schedule();return;}
        (void)summary.set(std::move(result.summary));
        if (!active_ || completion->version!=version_) {schedule();return;}
        (void)status.set(std::move(result.error));
        if (!active_ || completion->version!=version_) {schedule();return;}
        (void)plot.set(plots_[static_cast<std::size_t>(channel_)]);
        if (!active_ || completion->version!=version_) {schedule();return;}
        (void)busy.set(false);
    } else if (visible_ && source_) schedule();
    else (void)busy.set(false);
}
void HistogramModel::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Histogram requires its owner thread");
    if (!active_) return;
    active_=false;visible_=false;++version_;source_.reset();plots_={};
    if (bridge_) {std::lock_guard lock(bridge_->mutex);bridge_->closed=true;bridge_->completed.reset();}
    (void)sender_.cancel(wake_);executor_.reset();
    (void)busy.set(false);(void)plot.set({});(void)statistics.set({});(void)summary.set({});(void)status.set({});
}
}
