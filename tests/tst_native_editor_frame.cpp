#include "App/Views/Editor/NativeEditorFrame.h"
#include "App/Views/Editor/NativeLayerPanel.h"
#include <Document/Document.h>
#include <iiSDKCore>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <vector>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
class ManualExecutor final : public iiSharedCanvas::FrameRenderExecutor {
  public:
    void post(std::function<void()> task) override {
        if (reject) throw std::runtime_error("executor unavailable");
        tasks.push_back(std::move(task));
    }
    std::size_t maxConcurrency() const noexcept override {return 1;}
    void drain() {
        for (int count=0;!tasks.empty();++count) {
            if (count>100) throw std::runtime_error("render queue did not settle");
            auto task=std::move(tasks.front()); tasks.erase(tasks.begin()); task();
        }
    }
    bool reject=false;
    std::vector<std::function<void()>> tasks;
};
int main() {
    try {
        using namespace iiSharedCanvas;
        using namespace dreamscapes;
        auto session=native::DocumentSession::create();
        ii::runtime::EventLoop loop;
        auto worker=std::make_shared<ManualExecutor>();
        auto frame=editor::FramePresenter::create(session,loop,worker);
        good(frame->setViewport(100,100)); good(frame->setVisible(true));
        Document doc; doc.extent={2,2}; doc.timeline={{24,1},2};
        doc.assets.emplace_back(RasterAsset{"red",makeRasterLayer(2,2,0xffff0000)});
        doc.assets.emplace_back(RasterAsset{"blue",makeRasterLayer(2,2,0xff0000ff)});
        doc.layers.emplace_back(DynamicBitmapLayer{{"paint","Paint"},KeyframedSource{{0,1}}});
        doc.frames={{0,{{"paint","red"}}},{1,{{"paint","blue"}}}};
        good(session->attach(doc,"animated.iisc"));
        const auto snapshot=session->snapshot();
        check(frame->busy.get() && !frame->current.get() && !frame->image.get(),"initial frame was not scheduled");
        worker->drain(); (void)loop.poll();
        auto first=frame->image.get();
        check(frame->current.get() && first && first->rgba()[0]==255 && first->rgba()[2]==0 && first->rgba()[3]==255,
              "first frame did not render its actual pixels");
        good(session->seek(1));
        check(frame->busy.get() && !frame->current.get() && frame->image.get()==first && session->snapshot()==snapshot,
              "frame seek cleared pixels or copied the document");
        good(session->seek(0)); good(session->seek(1));
        worker->drain(); (void)loop.poll();
        auto second=frame->image.get();
        check(second && second!=first && second->rgba()[0]==0 && second->rgba()[2]==255 &&
              second->rgba()[3]==255,"the latest frame was not displayed");
        const auto beforeMetadata=session->revision.get(),visualBeforeMetadata=session->visualRevision.get();
        good(session->editPublication("title","Metadata only"));
        check(session->revision.get()==beforeMetadata+1 && session->visualRevision.get()==visualBeforeMetadata &&
              worker->tasks.empty() && !frame->busy.get() && frame->image.get()==second,
              "publication metadata caused a redundant canvas render");
        auto panel=editor::LayerPanelModel::create(session);
        good(session->selectLayer("paint")); good(panel->moveOpacity(50));
        check(session->visualRevision.get()>visualBeforeMetadata,"layer appearance did not advance visual revision");
        worker->drain(); (void)loop.poll();
        check(frame->image.get() && frame->image.get()->rgba()[3]>=126 && frame->image.get()->rgba()[3]<=129,
              "layer opacity edit did not reach the rendered pixels");
        good(panel->moveOpacity(100)); worker->drain(); (void)loop.poll();
        second=frame->image.get();
        worker->reject=true; good(session->seek(0)); (void)loop.poll();
        check(frame->image.get()==second && !frame->current.get() && !frame->busy.get() && !frame->status.get().empty(),
              "a renderer failure removed the preceding frame or hid the error");
        worker->reject=false; good(session->seek(1)); worker->drain(); (void)loop.poll();
        second=frame->image.get();
        good(session->resizeCanvasCentered(4,4));
        check(frame->image.get()==second && frame->busy.get(),"resizing cleared the visible frame before its replacement");
        worker->drain(); (void)loop.poll();
        auto enlarged=frame->image.get();
        check(enlarged && enlarged->rgba().size()>=64 && enlarged->rgba()[3]==0 &&
              enlarged->rgba()[22]==255 && enlarged->rgba()[23]==255,
              "centered canvas resizing did not move the rendered pixels into the larger extent");
        second=enlarged;
        good(session->setLayerVisible("paint",false));
        check(frame->image.get()==second && session->snapshot()!=snapshot,
              "an edit changed the worker snapshot or cleared the prior frame");
        worker->drain(); (void)loop.poll();
        check(frame->image.get() && frame->image.get()->rgba()[3]==0,
              "document visibility did not reach rendered pixels");
        Document reflection;reflection.extent={2,1};
        auto colors=makeRasterLayer(2,1,0xff000000);colors.pixels[0]=0xffff0000;colors.pixels[1]=0xff0000ff;
        reflection.assets.emplace_back(RasterAsset{"colors",std::move(colors)});
        reflection.layers.emplace_back(StaticBitmapLayer{{"colors-layer","Colors"},StaticSource{"colors"}});
        good(session->attach(reflection,"mirror.iisc"));worker->drain();(void)loop.poll();
        check(frame->image.get() && frame->image.get()->rgba()[0]==255,"mirror fixture did not render red first");
        good(session->mirrorCanvas(true));worker->drain();(void)loop.poll();
        check(frame->image.get() && frame->image.get()->rgba()[2]==255 && frame->image.get()->rgba()[4]==255,
              "the canvas mirror did not reverse the rendered pixels");
        good(session->rotateCanvas(90));worker->drain();(void)loop.poll();
        check(session->document()->extent.width==1 && session->document()->extent.height==2 &&
              frame->image.get() && frame->image.get()->rgba()[2]==255 && frame->image.get()->rgba()[4]==255,
              "canvas rotation did not produce the correct vertically ordered pixels");
        const auto newBlank=session->createLayer(iiSharedCanvas::LayerKind::StaticBitmap);
        check(bool(newBlank),"blank bitmap layer could not be created");
        worker->drain();(void)loop.poll();
        check(frame->image.get() && frame->image.get()->rgba()[2]==255 && frame->image.get()->rgba()[4]==255,
              "a blank bitmap layer obscured the existing canvas");
        Document other; other.extent={2,2};
        good(session->attach(other,"other.iisc"));
        check(!frame->image.get(),"the prior document remained visible after replacement");
        worker->drain(); (void)loop.poll();
        check(bool(frame->image.get()),"empty valid document did not render");
        frame->close(); worker->drain(); (void)loop.poll();
        ii::runtime::Runtime runtime(ii::runtime::RuntimeOptions{.workers=1,.capacity=8});
        editor::RuntimeFrameExecutor realExecutor(runtime);
        std::mutex mutex; std::condition_variable ready; bool completed=false,offThread=false;
        const auto caller=std::this_thread::get_id();
        realExecutor.post([&]{
            std::lock_guard lock(mutex); offThread=std::this_thread::get_id()!=caller; completed=true; ready.notify_one();
        });
        {std::unique_lock lock(mutex);
            check(ready.wait_for(lock,std::chrono::seconds(5),[&]{return completed;}) && offThread,
                  "the app renderer did not dispatch work off the view thread");
        }
        runtime.request_stop(ii::runtime::ShutdownMode::cancel_pending);
        check(bool(runtime.join()),"render worker did not shut down");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
