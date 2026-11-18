#include "App/Views/Editor/NativeEditorWorkspaceView.h"
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
    throw std::runtime_error("Missing brush widget "+std::string(id));
}
int main() {
    try {
        using namespace dreamscapes;using namespace iiSharedCanvas;
        ii::runtime::EventLoop loop;auto worker=std::make_shared<ManualExecutor>();
        auto document=native::DocumentSession::create();Document canvas;canvas.extent={16,16};
        canvas.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(16,16,0)});
        canvas.layers.emplace_back(StaticBitmapLayer{{"paint","Paint"},StaticSource{"pixels"}});
        good(document->attach(canvas,"brush.iisc"));good(document->selectLayer("paint"));
        auto workspace=editor::WorkspaceModel::create(document);
        auto presenter=editor::FramePresenter::create(document,loop,worker);
        auto brush=editor::BrushToolModel::create(document,loop,worker);
        BitmapBrush settings;settings.argb=0xffff0000;settings.size=4;good(brush->setBrush(settings));
        workspace->setFramePresenter(presenter);workspace->setBrushPanel(brush);workspace->setVisible(true);
        ii::gui::detail::WidgetFrameHooks hooks;hooks.events=[&]() -> ii::core::Result<ii::runtime::EventLoop *> {return &loop;};
        auto state=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();},ii::gui::WidgetAppearance{},hooks);
        auto root=take(state->content_host({},"root",{1200,900}));
        good(root.set_content([&](ii::gui::WidgetBuilder &builder){return editor::buildWorkspace(builder,{},workspace);}));
        (void)take(state->drawing_snapshot({1200,900}));
        auto drain=[&] {for (int count=0;!worker->tasks.empty();++count) {
            if (count>100) throw std::runtime_error("brush worker did not settle");
            auto task=std::move(worker->tasks.front());worker->tasks.erase(worker->tasks.begin());task();(void)loop.poll();
        }};
        drain();good(find(state,"editorBrushTool").activate());
        (void)take(state->drawing_snapshot({1200,900}));
        drain();
        check(workspace->activePanel.get()=="brush" && brush->available.get(),"brush panel is not mounted");
        const auto slider=take(find(state,"brushSize").snapshot()).bounds;
        check(slider.width>1 && slider.height>0,"brush size slider has no extent");
        const auto beforeSize=brush->size.get();
        good(state->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=slider.x+slider.width*.25,.y=slider.y+slider.height*.5,.buttons=1}));
        good(state->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=slider.x+slider.width*.25,.y=slider.y+slider.height*.5}));
        check(brush->size.get()!=beforeSize,"brush slider did not update its model");good(brush->setSize(4));
        (void)take(state->drawing_snapshot({1200,900}));
        const auto surface=take(find(state,"editorBrushSurface").snapshot()).bounds;
        const auto x=surface.x+surface.width*.5,y=surface.y+surface.height*.5;
        const auto original=document->snapshot();
        good(state->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=x,.y=y,.buttons=1}));drain();
        check(brush->previewImage.get() && workspace->renderedFrame.get()==brush->previewImage.get() && document->snapshot()==original,
              "brush did not present an uncommitted preview");
        good(state->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=x,.y=y}));drain();
        check(document->snapshot()!=original && document->dirty.get() && presenter->current.get() &&
              workspace->renderedFrame.get()==presenter->image.get(),"brush release did not update the canvas");
        state->close();workspace->setBrushPanel({});workspace->setFramePresenter({});brush->close();presenter->close();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
