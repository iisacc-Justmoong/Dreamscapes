#include "App/Views/Editor/NativeBrushTool.h"
#include <iiGUI>
#include <iostream>
#include <stdexcept>
#include <vector>

class ManualExecutor final : public iiSharedCanvas::FrameRenderExecutor {
  public:
    void post(std::function<void()> task) override {tasks.push_back(std::move(task));}
    std::size_t maxConcurrency() const noexcept override {return 1;}
    std::vector<std::function<void()>> tasks;
};
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
template<class T> T take(ii::core::Result<T> value) {if (!value) throw std::runtime_error(value.error().message);return std::move(*value);}
ii::gui::WidgetInput pointer(ii::gui::WidgetInputKind kind,double x,double y,double time) {
    ii::gui::WidgetInput input;input.kind=kind;input.local_position=ii::gui::WidgetPoint{x,y};
    input.device_id=7;input.buttons=kind==ii::gui::WidgetInputKind::pointer_up?0:1;
    input.pressure=1;input.timestamp=time;return input;
}
int main() {
    try {
        using namespace dreamscapes;using namespace iiSharedCanvas;
        Document canvas;canvas.extent={16,16};canvas.timeline.frameCount=2;
        canvas.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(16,16,0)});
        LayerProperties properties{"paint","Paint"};properties.transform.translationX=2;properties.transform.translationY=1;
        canvas.layers.emplace_back(StaticBitmapLayer{properties,StaticSource{"pixels"}});
        canvas.layers.emplace_back(StaticBitmapLayer{{"other","Other"},StaticSource{"pixels"}});
        auto document=native::DocumentSession::create();good(document->attach(canvas,"brush.iisc"));good(document->selectLayer("paint"));
        ii::runtime::EventLoop loop;auto worker=std::make_shared<ManualExecutor>();
        auto model=editor::BrushToolModel::create(document,loop,worker);
        model->setViewport(160,160);model->setVisible(true);
        model->setFrame(take(ii::gui::rgba_image(16,16,std::vector<std::uint8_t>(16*16*4,0))));
        BitmapBrush brush;brush.argb=0xffff0000;brush.size=4;good(model->setBrush(brush));
        check(model->colorText.get()=="#FF0000","brush color was not reflected in the panel");
        check(model->available.get(),"brush did not recognize the selected bitmap");
        auto drain=[&] {for (int count=0;!worker->tasks.empty();++count) {
            if (count>100) throw std::runtime_error("brush worker did not settle");
            auto task=std::move(worker->tasks.front());worker->tasks.erase(worker->tasks.begin());task();(void)loop.poll();
        }};
        const auto original=document->snapshot();
        check(take(model->input(pointer(ii::gui::WidgetInputKind::pointer_down,60,50,1))) && model->busy.get(),
              "brush did not capture the press");
        drain();
        check(model->previewImage.get() && document->snapshot()==original && !document->dirty.get(),
              "brush preview was not isolated from the document");
        check(take(model->input(pointer(ii::gui::WidgetInputKind::pointer_move,100,50,2))),"brush move was not captured");drain();
        check(take(model->input(pointer(ii::gui::WidgetInputKind::pointer_up,120,50,3))),"brush release was not captured");drain();
        const auto *layer=findStaticBitmapLayer(*document->document(),"paint");
        const auto *paint=layer?findRasterAsset(*document->document(),layer->content.assetId):nullptr;
        check(document->snapshot()!=original && document->dirty.get() && paint && paint->pixels.pixels[4*16+4]!=0 &&
              findRasterAsset(*document->document(),"pixels")->pixels.pixels[4*16+4]==0,
              "brush input did not map through the layer transform or preserve the shared asset");
        model->acknowledgeFrame();
        check(!model->busy.get() && !model->previewImage.get(),"completed preview was not retired after the new frame");
        const auto current=document->snapshot();
        check(take(model->input(pointer(ii::gui::WidgetInputKind::pointer_down,60,70,4))),"second brush press failed");
        good(document->selectLayer("other"));drain();
        check(document->snapshot()==current && !model->previewImage.get() && !model->busy.get(),
              "selection change allowed stale brush work to commit");
        model->close();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
