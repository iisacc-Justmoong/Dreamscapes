#include "App/Views/Editor/NativeHistogram.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

class ManualExecutor final : public iiSharedCanvas::FrameRenderExecutor {
  public:
    void post(std::function<void()> task) override {tasks.push_back(std::move(task));}
    std::size_t maxConcurrency() const noexcept override {return 1;}
    std::vector<std::function<void()>> tasks;
};
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes::editor;
        ii::runtime::EventLoop loop;auto executor=std::make_shared<ManualExecutor>();
        auto model=HistogramModel::create(loop,executor);
        bool rejectedWrongThread=false;
        std::thread other([&]{rejectedWrongThread=!model->select(HistogramChannel::blue);});other.join();
        check(rejectedWrongThread && model->rgbSelected.get(),"histogram accepted a non-owner-thread edit");
        auto image=ii::gui::rgba_image(2,2,{255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,0});
        check(bool(image),"fixture image could not be built");
        good(model->setSource(*image));
        check(executor->tasks.empty(),"hidden histogram consumed a frame");
        good(model->setVisible(true));
        check(executor->tasks.size()==1 && model->busy.get() && !model->plot.get(),"histogram did not dispatch to a worker");
        auto run=[&] {check(executor->tasks.size()==1,"expected one histogram worker");auto task=std::move(executor->tasks.front());executor->tasks.clear();task();(void)loop.poll();};
        run();
        const auto stats=model->statistics.get();
        check(!model->busy.get() && model->plot.get() && stats.pixels==3 &&
              stats.red[0]==2 && stats.red[255]==1 && stats.green[0]==2 && stats.green[255]==1 &&
              stats.blue[0]==2 && stats.blue[255]==1 && std::abs(stats.mean-85.0)<0.001 &&
              model->summary.get().find("preview pixels")!=std::string::npos,
              "histogram bins or transparent-pixel treatment are incorrect");
        const auto composite=model->plot.get();good(model->select(HistogramChannel::red));
        check(model->plot.get() && model->plot.get()!=composite && model->redSelected.get() && executor->tasks.empty(),
              "channel selection recomputed the frame or used the wrong chart");
        auto old=ii::gui::rgba_image(1,1,{0,0,0,255}),latest=ii::gui::rgba_image(1,1,{128,128,128,255});
        check(bool(old) && bool(latest),"additional histogram fixtures failed");
        good(model->setSource(*old));good(model->setSource(*latest));
        check(executor->tasks.size()==1,"histogram did not coalesce frame requests");
        run();check(executor->tasks.size()==1 && model->statistics.get().pixels==3,
                    "a stale frame replaced the current histogram");
        run();check(model->statistics.get().pixels==1 && model->statistics.get().red[128]==1 &&
                    model->statistics.get().red[0]==0,"the newest frame was not projected");
        good(model->setSource(*old));good(model->setSource({}));
        check(!model->plot.get() && !model->busy.get() && model->statistics.get().pixels==0,
              "removing the displayed frame retained an obsolete histogram");
        run();check(!model->plot.get() && executor->tasks.empty(),
                    "an obsolete frame restored a histogram after document removal");
        good(model->setSource(*latest));run();
        auto transparent=ii::gui::rgba_image(1,1,{255,255,255,0});
        check(bool(transparent),"transparent fixture failed");
        good(model->setSource(*transparent));run();
        check(!model->plot.get() && model->statistics.get().pixels==0 &&
              model->summary.get()=="No visible preview pixels","transparent-only preview reported fabricated data");
        good(model->setVisible(false));good(model->setSource(*old));
        check(executor->tasks.empty(),"hidden histogram scheduled work");
        good(model->setVisible(true));check(executor->tasks.size()==1,"histogram did not refresh on return");
        model->close();run();
        check(!model->plot.get() && !model->busy.get(),"a closed histogram accepted a late result");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
}
