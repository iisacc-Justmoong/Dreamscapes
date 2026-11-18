#include "App/Views/Editor/NativeBrushQueue.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

class ManualExecutor final : public iiSharedCanvas::FrameRenderExecutor {
  public:
    bool reject=false;
    void post(std::function<void()> task) override {
        if (reject) throw std::runtime_error("worker unavailable");tasks.push_back(std::move(task));
    }
    std::size_t maxConcurrency() const noexcept override {return 1;}
    std::vector<std::function<void()>> tasks;
};
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
template<class T> T take(ii::core::Result<T> value) {if (!value) throw std::runtime_error(value.error().message);return std::move(*value);}

int main() {
    try {
        using namespace dreamscapes::editor;using namespace iiSharedCanvas;
        auto document=std::make_shared<Document>();document->extent={16,16};
        document->assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(16,16,0)});
        document->layers.emplace_back(StaticBitmapLayer{{"paint","Paint"},StaticSource{"pixels"}});
        ii::runtime::EventLoop loop;auto executor=std::make_shared<ManualExecutor>();
        std::vector<BrushQueueUpdate> updates;
        auto queue=BrushQueue::create(loop,executor,[&](BrushQueueUpdate result){updates.push_back(std::move(result));});
        BitmapBrush brush;brush.argb=0xffff0000;brush.size=4;
        BrushQueueRequest request{document,1,2,"paint",0,brush,16,16};
        auto step=[&] {check(!executor->tasks.empty(),"missing brush worker");auto task=std::move(executor->tasks.front());
            executor->tasks.erase(executor->tasks.begin());task();(void)loop.poll();};
        const auto id=take(queue->begin(request,{.position={4,4},.pressure=1,.time=1}));
        check(updates.empty() && executor->tasks.size()==1 && findRasterAsset(*document,"pixels")->pixels.pixels[4*16+4]==0,
              "brush work ran inline or modified the source");
        step();
        check(updates.size()==1 && !updates.back().complete && updates.back().image &&
              updates.back().image->rgba()[(4*16+4)*4]>0,"the first brush preview was not rendered");
        good(queue->move(id,{.position={8,4},.pressure=1,.time=2}));step();
        check(updates.back().image && updates.back().image->rgba()[(4*16+8)*4]>0 && !updates.back().complete,
              "a streamed sample did not update the preview");
        good(queue->finish(id,{.position={10,4},.pressure=1,.time=3}));step();
        check(updates.back().complete && updates.back().prepared && updates.back().error.empty() &&
              findRasterAsset(*document,"pixels")->pixels.pixels[4*16+4]==0,
              "finished work did not return an isolated candidate");
        check(!queue->move(id,{.position={1,1},.pressure=1,.time=4}),"retired brush accepted an event");
        const auto cancelled=take(queue->begin(request,{.position={2,2},.pressure=1,.time=1}));
        queue->cancel();while (!executor->tasks.empty()) step();
        check(!queue->move(cancelled,{.position={3,3},.pressure=1,.time=2}) && updates.back().requestId==id,
              "a cancelled worker published a result");
        executor->reject=true;
        check(!queue->begin(request,{.position={2,2},.pressure=1,.time=1}),"a rejected worker was reported as accepted");
        executor->reject=false;
        const auto invalid=take(queue->begin(request,{.position={2,2},.pressure=1,.time=1}));step();
        good(queue->move(invalid,{.position={std::numeric_limits<double>::quiet_NaN(),2},.pressure=1,.time=2}));step();
        check(updates.back().complete && !updates.back().prepared && !updates.back().error.empty(),
              "worker input failure was not delivered");
        queue->close();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
