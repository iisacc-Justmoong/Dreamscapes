#include "App/Views/Editor/NativeLightPanel.h"
#include <iostream>
#include <limits>
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
int main() {
    try {
        using namespace dreamscapes;using namespace iiSharedCanvas;
        ii::runtime::EventLoop loop;auto worker=std::make_shared<ManualExecutor>();
        auto document=native::DocumentSession::create();
        Document canvas;canvas.extent={2,2};canvas.timeline.frameCount=2;
        canvas.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(2,2,0xff404040)});
        canvas.layers.emplace_back(StaticBitmapLayer{{"paint","Paint"},StaticSource{"pixels"}});
        canvas.layers.emplace_back(StaticBitmapLayer{{"shared","Shared",false},StaticSource{"pixels"}});
        good(document->attach(canvas,"light.iisc"));good(document->selectLayer("paint"));
        constexpr auto catalog=R"JSON([{"key":"color","fields":[{"id":"field-1","label":"Exposure","minimum":-5,"maximum":5,"step":0.01},{"id":"field-4","label":"Contrast","minimum":-100,"maximum":100,"step":1},{"id":"field-8","label":"Highlights","minimum":-100,"maximum":100,"step":1},{"id":"field-12","label":"Shadows","minimum":-100,"maximum":100,"step":1},{"id":"field-16","label":"Whites","minimum":-100,"maximum":100,"step":1},{"id":"field-20","label":"Blacks","minimum":-100,"maximum":100,"step":1}]}])JSON";
        auto model=editor::LightPanelModel::create(document,loop,worker,catalog);model->setVisible(true);model->setViewport(100,100);
        auto exposure=model->field("exposure");
        check(exposure && model->fields.get().size()==6 && model->available.get(),"light controls did not project the selected bitmap");
        auto step=[&] {check(!worker->tasks.empty(),"missing light worker");auto task=std::move(worker->tasks.front());
            worker->tasks.erase(worker->tasks.begin());task();(void)loop.poll();};
        const auto initial=document->snapshot();
        good(model->requestAuto());
        check(model->busy.get() && worker->tasks.size()==1 && document->snapshot()==initial,
              "auto light analysis did not run on a worker");
        step();
        check(!model->busy.get() && document->snapshot()==initial && !document->dirty.get() &&
              model->canPreview.get() && !model->canApply.get(),
              "auto light analysis did not produce an uncommitted draft");
        good(model->requestPreview());step();
        bool redirectedDuringAuto=false;
        auto previewRedirect=model->previewImage.observe([&](const auto &image,std::uint64_t) {
            if (!image && !redirectedDuringAuto) {redirectedDuringAuto=true;good(document->selectLayer("shared"));}
        });
        check(!model->requestAuto() && redirectedDuringAuto && document->selectedLayer.get()=="shared" &&
              !model->busy.get() && worker->tasks.size()==1,
              "a reentrant selection change started analysis for another layer");
        step();previewRedirect={};good(document->selectLayer("paint"));
        good(model->requestAuto());step();good(model->requestPreview());step();
        bool redirectedDuringEdit=false;
        auto editRedirect=model->previewImage.observe([&](const auto &image,std::uint64_t) {
            if (!image && !redirectedDuringEdit) {redirectedDuringEdit=true;good(document->selectLayer("shared"));}
        });
        check(!exposure->setValue(2) && redirectedDuringEdit && document->selectedLayer.get()=="shared" &&
              exposure->value.get()==0 && exposure->text.get()=="0",
              "a reentrant selection change applied a slider edit to another layer");
        step();editRedirect={};good(document->selectLayer("paint"));
        good(model->requestAuto());step();good(model->requestPreview());step();
        bool redirectedDuringInput=false;
        auto inputRedirect=model->previewImage.observe([&](const auto &image,std::uint64_t) {
            if (!image && !redirectedDuringInput) {redirectedDuringInput=true;good(document->selectLayer("shared"));}
        });
        exposure->edited("1.5");
        check(redirectedDuringInput && document->selectedLayer.get()=="shared" &&
              exposure->value.get()==0 && exposure->text.get()=="0" && !model->canPreview.get(),
              "a reentrant selection change applied typed input to another layer");
        step();inputRedirect={};good(document->selectLayer("paint"));
        good(model->requestAuto());
        good(document->selectLayer("shared"));step();
        check(document->snapshot()==initial && exposure->value.get()==0 && !model->canApply.get(),
              "late auto light analysis changed another layer's draft");
        good(document->selectLayer("paint"));good(exposure->setValue(1));
        check(document->snapshot()==initial && !document->dirty.get() && !model->canApply.get(),
              "changing a light slider modified the document or applied an unprepared draft");
        good(model->requestPreview());
        check(model->busy.get() && worker->tasks.size()==1 && document->snapshot()==initial,"preview did not use the worker");
        step();check(!model->busy.get() && model->previewImage.get() && model->previewImage.get()->rgba()[0]>64 &&
                     model->canApply.get() && document->snapshot()==initial,
                     "a completed preview modified the document or did not become applicable");
        model->setViewport(50,50);
        check(!model->previewImage.get() && !model->canApply.get() && document->snapshot()==initial,
              "resizing retained a preview rendered for another viewport");
        if (!worker->tasks.empty()) step(); // Retire the old candidate off the view thread.
        good(model->requestPreview());step();
        good(model->apply());
        check(document->snapshot()!=initial && document->dirty.get() && document->selectedLayer.get()=="paint",
              "applying the preview did not edit the selected canvas layer");
        good(exposure->setValue(2));good(model->requestPreview());
        good(document->selectLayer("shared"));const auto changed=document->snapshot();step();
        check(document->snapshot()==changed && !model->previewImage.get() && !model->canApply.get(),
              "a late preview applied to a different selection");
        good(document->selectLayer("paint"));
        check(!exposure->setValue(std::numeric_limits<double>::quiet_NaN()),"an invalid slider value was accepted");
        exposure->edited("not a number");exposure->submitted();
        check(exposure->invalid.get() && !model->canPreview.get(),"an invalid typed value could be previewed");
        exposure->edited("1.5");exposure->submitted();
        check(!exposure->invalid.get() && exposure->value.get()==1.5 && model->canPreview.get(),
              "a valid typed value did not update the light draft");
        bool redirected=false;
        auto redirect=exposure->value.observe([&](double value,std::uint64_t) {
            if (value==2 && !redirected) {redirected=true;good(document->selectLayer("shared"));}
        });
        check(!exposure->setValue(2) && redirected && document->selectedLayer.get()=="shared" &&
              exposure->value.get()==0 && !model->canApply.get(),
              "a reentrant selection change retained another layer's light draft");
        model->close();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
