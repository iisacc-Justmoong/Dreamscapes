#include "NativeEditorWorkspaceView.h"
#include "NativeEditorWorkspace.generated.h"

std::function<void(double,double)> NativeEditorWorkspaceBackend::sizeHandler(double maximumPanel,double toolbarWidth,double gaps) {
    const std::weak_ptr<dreamscapes::editor::WorkspaceModel> weak=model;
    return [weak,maximumPanel,toolbarWidth,gaps](double width,double height) {if (auto value=weak.lock()) value->sizeChanged(width,height,maximumPanel,toolbarWidth,gaps);};
}
void NativeEditorWorkspaceBackend::attachTimelineMenu(ii::gui::Widget menu,ii::gui::Widget anchor) {
    timelineMenu_=std::move(menu);timelineAnchor_=std::move(anchor);
}
void NativeEditorWorkspaceBackend::openTimelineMenu() {
    const auto anchor=timelineAnchor_.snapshot();
    if (anchor && anchor->visible && anchor->enabled) (void)timelineMenu_.open_for(timelineAnchor_,0,anchor->bounds.height);
}
namespace dreamscapes::editor {
std::shared_ptr<PanelSource> PanelSource::create(const std::shared_ptr<WorkspaceModel> &model) {
    auto source=std::shared_ptr<PanelSource>(new PanelSource);
    source->file_=filePanelSource(model->filePanel());source->layer_=layerPanelSource(model->layerPanel());
    source->canvas_=canvasPanelSource(model->canvasPanel());
    source->elements_=elementsPanelSource(model->elementsPanel());
    source->mask_=maskPanelSource(model->maskPanel());
    const std::weak_ptr<PanelSource> weak=source;
    source->active_=model->activePanel.observe([weak](const std::string &panel,std::uint64_t) {
        if (auto self=weak.lock()) (void)self->current.set(panel=="file"?self->file_:panel=="canvas"?self->canvas_:
            panel=="elements"?self->elements_:panel=="mask"?self->mask_:self->layer_);
    },true);
    return source;
}
ii::core::Result<void> buildWorkspace(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<WorkspaceModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Editor workspace requires a model");
    auto host=builder.content_host(parent,"editorWorkspaceHost");if (!host) return std::unexpected(host.error());
    if (auto flex=host->set_flex(1);!flex) return flex;
    return host->set_content([host=*host,model=std::move(model)](ii::gui::WidgetBuilder &widgets) {
        using View=lvui_generated::NativeEditorWorkspace;
        std::shared_ptr<View::Scope> scope;
        auto factories=lvui::native::BackendFactories{}.with<NativeEditorWorkspaceBackend>([model]{return NativeEditorWorkspaceBackend(model);});
        if (auto built=View::build(widgets,{},[](auto &) {},&scope,factories);!built) return built;
        return widgets.on_publish([host,weak=std::weak_ptr(scope)] {
            if (auto owner=weak.lock()) {
                auto menu=host.content_item("timelineNewMenu"),anchor=host.content_item("timelineNewLayer");
                if (menu && anchor) owner->backend_0.attachTimelineMenu(*menu,*anchor);
            }
        });
    });
}
}
