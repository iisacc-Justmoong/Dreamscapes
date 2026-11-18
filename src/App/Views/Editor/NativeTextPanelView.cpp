#include "NativeTextPanelView.h"
#include "NativeTextPanel.generated.h"
#include <iiGUI>
#include <algorithm>
#include <cmath>

namespace dreamscapes::editor {
ii::core::Result<void> buildTextPanel(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<TextToolModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Text panel requires a model");
    auto host=builder.content_host(parent,"textPanelHost");if (!host) return std::unexpected(host.error());
    if (auto flex=host->set_flex(1);!flex) return flex;
    auto layout=std::make_shared<TextPanelLayout>();
    return host->set_content([model=std::move(model),layout=std::move(layout)](ii::gui::WidgetBuilder &widgets) {
        auto factories=lvui::native::BackendFactories{}.with<NativeTextPanelBackend>([model,layout]{return NativeTextPanelBackend(model,layout);});
        return lvui_generated::NativeTextPanel::build(widgets,{},[](auto &) {},nullptr,factories);
    });
}
std::shared_ptr<ii::gui::NativeViewSource> textPanelSource(std::shared_ptr<TextToolModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder){return buildTextPanel(builder,{},model);});
}
namespace {
struct SurfaceBinding final {
    std::shared_ptr<TextToolModel> model;ii::gui::Widget surface;
    ii::core::ObservableSubscription width,height;
};
}
std::shared_ptr<ii::gui::NativeViewSource> textSurfaceSource(std::shared_ptr<TextToolModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder)->ii::core::Result<void> {
        if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Text surface requires a model");
        auto surface=builder.absolute_stack({},"editorTextSurface",{model->width.get(),model->height.get()});
        if (!surface) return std::unexpected(surface.error());
        auto binding=std::make_shared<SurfaceBinding>();binding->model=model;binding->surface=*surface;
        const std::weak_ptr<SurfaceBinding> weak=binding;
        if (auto input=surface->set_input_handler([weak](const auto &event)->ii::core::Result<bool> {
            if (auto self=weak.lock()) return self->model->input(event);return false;
        });!input) return input;
        auto resize=[weak](const auto &,std::uint64_t) {
            if (auto self=weak.lock()) (void)self->surface.set_extent({self->model->width.get(),self->model->height.get()});
        };
        binding->width=model->width.observe(resize);binding->height=model->height.observe(resize);
        if (auto retained=builder.retain(binding);!retained) return retained;
        return builder.on_retire([weak]{if (auto self=weak.lock()) self->model->cancel();});
    });
}
}
NativeTextPanelBackend::NativeTextPanelBackend(std::shared_ptr<dreamscapes::editor::TextToolModel> value,
    std::shared_ptr<dreamscapes::editor::TextPanelLayout> dimensions)
    :model(std::move(value)),layout(std::move(dimensions)),panelWidth(layout->width),panelHeight(layout->height),contentWidth(layout->contentWidth),
     size(model->fontSize),content(model->content),status(model->status),familyDraft(model->family.get()),colorDraft(model->colorText.get()),
     placeholder(model->defaults.placeholder),minimum(model->defaults.minimum),maximum(model->defaults.maximum),step(model->defaults.step) {}
std::function<void(double,double)> NativeTextPanelBackend::sizeHandler(double inset) {
    return [layout=std::weak_ptr(layout),inset](double width,double height) {
        if (auto value=layout.lock();value && std::isfinite(width) && std::isfinite(height) && width>=0 && height>=0) {
            (void)value->width.set(width);(void)value->height.set(height);
            (void)value->contentWidth.set(std::clamp(width-inset,lvui::SliderConfiguration{}.thumb_diameter,8192.0));
        }
    };
}
void NativeTextPanelBackend::contentEdited(std::string value) {
    const auto result=model->setContent(std::move(value));(void)model->status.set(result?std::string{}:result.error().message);
}
void NativeTextPanelBackend::familyEdited(std::string value) {
    (void)familyDraft.set(value);
    if (!value.empty()) {const auto result=model->setFamily(std::move(value));if (result) (void)model->status.set({});}
}
void NativeTextPanelBackend::familySubmitted() {
    const auto result=model->setFamily(familyDraft.get());(void)model->status.set(result?std::string{}:result.error().message);
}
void NativeTextPanelBackend::colorEdited(std::string value) {
    (void)colorDraft.set(value);
    const auto result=model->setColor(std::move(value));if (result) (void)model->status.set({});
}
void NativeTextPanelBackend::colorSubmitted() {
    const auto result=model->setColor(colorDraft.get());(void)model->status.set(result?std::string{}:result.error().message);
}
void NativeTextPanelBackend::sizeMoved(double value) {
    const auto result=model->setFontSize(value);(void)model->status.set(result?std::string{}:result.error().message);
}
