#include "App/Views/Editor/NativeEditorWorkspaceView.h"
#include "NativeAssets.h"
#include <iiGUI/WidgetState.h>
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
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state,std::string_view id) {
    for (const auto &node:take(state->snapshots()))
        if (node.id==id || node.id.ends_with(std::string("/")+std::string(id))) return take(state->find(node.id));
    throw std::runtime_error("Missing light widget "+std::string(id));
}
int main() {
    try {
        using namespace dreamscapes;using namespace iiSharedCanvas;
        ii::runtime::EventLoop loop;auto worker=std::make_shared<ManualExecutor>();
        auto document=native::DocumentSession::create();Document canvas;canvas.extent={2,2};
        canvas.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(2,2,0xff404040)});
        canvas.layers.emplace_back(StaticBitmapLayer{{"paint","Paint"},StaticSource{"pixels"}});
        good(document->attach(canvas,"light.iisc"));good(document->selectLayer("paint"));
        auto workspace=editor::WorkspaceModel::create(document);
        auto presenter=editor::FramePresenter::create(document,loop,worker);
        auto model=editor::LightPanelModel::create(document,loop,worker,native::toolCatalog);
        workspace->setFramePresenter(presenter);workspace->setLightPanel(model);workspace->setVisible(true);
        ii::gui::detail::WidgetFrameHooks hooks;hooks.events=[&]() -> ii::core::Result<ii::runtime::EventLoop *> {return &loop;};
        auto state=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();},ii::gui::WidgetAppearance{},hooks);
        auto root=take(state->content_host({},"root",{1200,900}));
        good(root.set_content([&](ii::gui::WidgetBuilder &builder){return editor::buildWorkspace(builder,{},workspace);}));
        (void)take(state->drawing_snapshot({1200,900}));
        auto drain=[&] {for (int count=0;!worker->tasks.empty();++count) {
            if (count>100) throw std::runtime_error("light worker did not settle");
            auto task=std::move(worker->tasks.front());worker->tasks.erase(worker->tasks.begin());task();(void)loop.poll();
        }};
        drain();good(find(state,"editorLightTool").activate());
        (void)take(state->drawing_snapshot({1200,900}));
        check(workspace->activePanel.get()=="light" && model->available.get(),"the editor did not mount the live light panel");
        auto exposure=model->field("exposure");check(bool(exposure),"light exposure control is missing");
        const auto original=document->snapshot();
        good(find(state,"lightAuto").activate());drain();
        check(model->canPreview.get() && document->snapshot()==original && !document->dirty.get(),
              "the Auto button did not create an uncommitted light draft");
        good(find(state,"lightReset").activate());
        const auto slider=take(find(state,"lightSlider").snapshot()).bounds;
        check(slider.width>2 && slider.height>0,"the light slider has no usable extent");
        good(state->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=slider.x+slider.width*.8,.y=slider.y+slider.height*.5,.buttons=1}));
        good(state->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=slider.x+slider.width*.8,.y=slider.y+slider.height*.5}));
        check(exposure->value.get()!=0 && document->snapshot()==original,"the light slider did not edit only the draft");
        good(exposure->setValue(1));
        (void)take(state->drawing_snapshot({1200,900}));good(find(state,"lightPreview").activate());drain();
        check(model->previewImage.get() && workspace->renderedFrame.get()==model->previewImage.get() &&
              document->snapshot()==original && !document->dirty.get(),"the editor did not display the uncommitted preview");
        (void)take(state->drawing_snapshot({1200,900}));good(find(state,"lightApply").activate());drain();
        check(document->snapshot()!=original && document->dirty.get() && presenter->current.get() &&
              workspace->renderedFrame.get()==presenter->image.get(),"the light Apply button did not update the canvas renderer");
        state->close();workspace->setLightPanel({});workspace->setFramePresenter({});model->close();presenter->close();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
