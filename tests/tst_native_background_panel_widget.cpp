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
    const auto snapshots=take(state->snapshots());
    for (const auto &node:snapshots)
        if (node.id==id || node.id.ends_with(std::string("/")+std::string(id))) return take(state->find(node.id));
    for (const auto &node:snapshots) if (node.id.find("background")!=std::string::npos || node.id.find("mask")!=std::string::npos || node.id.find("editorPanel")!=std::string::npos)
        std::cerr<<node.id<<" "<<node.text<<'\n';
    throw std::runtime_error("Missing background widget "+std::string(id));
}
int main() {
    try {
        using namespace dreamscapes;
        constexpr auto defaults=R"JSON({"color":{"backgroundColor":"#8066FF","backgroundAlpha":100,"backgroundOpacity":100,"backgroundPickerMode":0,"backgroundFill":"Solid","backgroundGradientStops":[{"position":0,"color":"#000000"},{"position":1,"color":"#FFFFFF"}],"backgroundGradientType":"Linear","backgroundInterpolation":"Perceptual","backgroundAngle":90},"presets":[{"name":"Mono","colors":["#000000","#FFFFFF"]}]})JSON";
        ii::runtime::EventLoop loop;auto worker=std::make_shared<ManualExecutor>();
        auto document=native::DocumentSession::create();iiSharedCanvas::Document canvas;canvas.extent={6,4};
        good(document->attach(canvas,"widget.iisc"));
        auto workspace=editor::WorkspaceModel::create(document);
        auto model=editor::BackgroundPanelModel::create(document,loop,worker,defaults);
        {
            ii::gui::detail::WidgetFrameHooks panelHooks;panelHooks.events=[&]() -> ii::core::Result<ii::runtime::EventLoop *> {return &loop;};
            auto panelState=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();},ii::gui::WidgetAppearance{},panelHooks);
            auto panelRoot=take(panelState->content_host({},"panelRoot",{280,900}));
            good(panelRoot.set_content([&](ii::gui::WidgetBuilder &builder){return editor::buildBackgroundPanel(builder,{},model);}));
            (void)take(panelState->drawing_snapshot({280,900}));
            (void)find(panelState,"backgroundPickerMode");panelState->close();
        }
        auto presenter=editor::FramePresenter::create(document,loop,worker);
        workspace->setFramePresenter(presenter);workspace->setBackgroundPanel(model);workspace->setVisible(true);
        ii::gui::detail::WidgetFrameHooks hooks;hooks.events=[&]() -> ii::core::Result<ii::runtime::EventLoop *> {return &loop;};
        auto state=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();},ii::gui::WidgetAppearance{},hooks);
        auto root=take(state->content_host({},"root",{1200,900}));
        good(root.set_content([&](ii::gui::WidgetBuilder &builder){return editor::buildWorkspace(builder,{},workspace);}));
        (void)take(state->drawing_snapshot({1200,900}));
        good(find(state,"editorBackgroundTool").activate());
        (void)take(state->drawing_snapshot({1200,900}));
        good(find(state,"maskColorTab").activate());
        (void)take(state->drawing_snapshot({1200,900}));
        check(workspace->activePanel.get()=="background-color" && take(find(state,"backgroundPickerMode").snapshot()).enabled,
              "the actual editor did not mount the color viewmodel");
        auto drain=[&] {while (!worker->tasks.empty()) {auto task=std::move(worker->tasks.front());worker->tasks.erase(worker->tasks.begin());task();(void)loop.poll();}};
        drain();(void)take(state->drawing_snapshot({1200,900}));
        const auto prior=model->hex.get();const auto bounds=take(find(state,"backgroundPlaneSurface").snapshot()).bounds;
        check(bounds.width>2 && bounds.height>2,"the native color surface has no usable extent");
        good(state->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=bounds.x+bounds.width*.2,.y=bounds.y+bounds.height*.2,.buttons=1}));
        good(state->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=bounds.x+bounds.width*.2,.y=bounds.y+bounds.height*.2,.buttons=1}));
        check(model->hex.get()!=prior && !document->dirty.get(),"picker input did not update only the draft");
        good(find(state,"backgroundApply").activate());drain();
        check(document->dirty.get() && iiSharedCanvas::findLayer(*document->document(),"canvas.background.layer"),
              "the native Apply button did not update the canvas");
        check(presenter->current.get() && presenter->image.get() && presenter->image.get()->rgba()[3]==255,
              "the background edit did not reach the actual canvas renderer");
        (void)take(state->drawing_snapshot({1200,900}));good(find(state,"backgroundMaskTab").activate());
        check(workspace->maskPanelActive.get(),"the native Mask button did not return to the mask view");
        bool choseImage=false;model->setImagePickHandler([&]() -> ii::core::Result<void> {choseImage=true;return {};});
        (void)take(state->drawing_snapshot({1200,900}));good(find(state,"maskReplaceTab").activate());
        (void)take(state->drawing_snapshot({1200,900}));
        check(model->replaceMode.get() && workspace->backgroundPanelActive.get(),"the native Replace tab did not mount its model");
        good(find(state,"backgroundChooseImage").activate());check(choseImage,"the Replace image button did not invoke its picker");
        (void)find(state,"backgroundImageFit");
        state->close();workspace->setFramePresenter({});presenter->close();workspace->setBackgroundPanel({});model->close();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
