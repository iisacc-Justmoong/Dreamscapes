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
    throw std::runtime_error("Missing audio widget "+std::string(id));
}
int main() {
    try {
        using namespace dreamscapes;
        ii::runtime::EventLoop loop;auto worker=std::make_shared<ManualExecutor>();
        auto document=native::DocumentSession::create();iiSharedCanvas::Document canvas;canvas.extent={4,4};canvas.timeline={{24,1},24};
        good(document->attach(canvas,"audio.iisc"));
        auto workspace=editor::WorkspaceModel::create(document);auto model=editor::AudioPanelModel::create(document,loop,worker);
        workspace->setAudioPanel(model);workspace->setVisible(true);
        bool picker=false;model->setPickHandler([&]() -> ii::core::Result<void> {picker=true;return {};});
        ii::gui::detail::WidgetFrameHooks hooks;hooks.events=[&]() -> ii::core::Result<ii::runtime::EventLoop *> {return &loop;};
        auto state=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();},ii::gui::WidgetAppearance{},hooks);
        auto root=take(state->content_host({},"root",{1200,900}));
        good(root.set_content([&](ii::gui::WidgetBuilder &builder){return editor::buildWorkspace(builder,{},workspace);}));
        (void)take(state->drawing_snapshot({1200,900}));good(find(state,"editorAudioTool").activate());
        (void)take(state->drawing_snapshot({1200,900}));
        check(workspace->audioPanelActive.get(),"audio toolbar did not open the live panel");
        good(find(state,"audioCreateTrack").activate());
        check(model->busy.get() && document->document()->audioTracks.empty(),"new track did not use the worker");
        check(worker->tasks.size()==1,"audio create was not scheduled");auto task=std::move(worker->tasks.front());worker->tasks.clear();task();(void)loop.poll();
        (void)take(state->drawing_snapshot({1200,900}));
        good(find(state,"audioChooseFile").activate());check(picker,"import button did not invoke the picker");
        good(find(state,"audioMixTab").activate());(void)take(state->drawing_snapshot({1200,900}));
        auto gain=find(state,"audioGain");good(gain.focus());
        good(state->input({.kind=ii::gui::WidgetInputKind::key_down,.key=ii::gui::WidgetKey::select_all}));
        good(state->input({.kind=ii::gui::WidgetInputKind::text,.text="-6"}));
        check(model->gainText.get()=="-6" && model->canApply.get(),"gain input did not update the audio draft");
        good(find(state,"audioApply").activate());check(worker->tasks.size()==1,"audio Apply did not use the worker");
        task=std::move(worker->tasks.front());worker->tasks.clear();task();(void)loop.poll();
        check(document->document()->audioTracks.front().gainDb==-6,"audio Apply did not update the selected track");
        iiSharedCanvas::AudioAsset asset{"source",48000,1,std::vector<std::int16_t>(48000,123)};
        auto imported=editor::prepareAudioImport(document->snapshot(),document->documentGeneration.get(),document->revision.get(),
                                                  document->document()->audioTracks.front().id,0,std::move(asset),"Voice");
        check(bool(imported),"audio widget clip fixture failed");good(document->commitAudioEdit(std::move(*imported)));
        good(find(state,"audioTrimTab").activate());(void)take(state->drawing_snapshot({1200,900}));
        (void)find(state,"audioClipSelector");good(model->selectClip(1));
        auto range=find(state,"audioTrimRange");good(range.focus());
        good(state->input({.kind=ii::gui::WidgetInputKind::key_down,.key=ii::gui::WidgetKey::select_all}));
        good(state->input({.kind=ii::gui::WidgetInputKind::text,.text="0.25 - 0.75"}));
        check(model->canTrim.get(),"trim range input did not update the clip draft");
        good(find(state,"audioTrimApply").activate());check(worker->tasks.size()==1,"trim Apply did not use the worker");
        task=std::move(worker->tasks.front());worker->tasks.clear();task();(void)loop.poll();
        check(document->document()->audioTracks.front().clips.front().startFrame==6 &&
              document->document()->audioTracks.front().clips.front().durationFrames==12,
              "trim Apply did not update the selected clip");
        state->close();workspace->setAudioPanel({});model->close();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
