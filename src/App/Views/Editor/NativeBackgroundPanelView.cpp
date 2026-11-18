#include "NativeBackgroundPanelView.h"
#include "NativeBackgroundPanel.generated.h"
#include <LVUI>
#include <iiGUI>
#include <algorithm>
#include <cmath>

std::function<void(double,double)> NativeBackgroundPanelBackend::sizeHandler(double inset,double rail,double gap,double rowHeight) {
    return [weak=std::weak_ptr(model),inset,rail,gap,rowHeight](double width,double height) {
        if (auto value=weak.lock()) value->sizeChanged(width,height,inset,rail,gap,rowHeight);
    };
}
namespace dreamscapes::editor {
namespace {
using Image=std::shared_ptr<const ii::gui::RasterImage>;
struct Binding final {
    std::shared_ptr<BackgroundPanelModel> model;
    ii::gui::Widget widget;
    Image indicator;
    ii::core::ObservableSubscription image,version,width,height,marker;
    bool rail=false,dragging=false;
};
Image indicatorImage() {
    constexpr int size=9;
    const auto dark=lvui::figma_defaults::panel_surface_ghost;
    const auto light=lvui::figma_defaults::text_title_header_color;
    const auto byte=[](double value){return static_cast<std::uint8_t>(std::lround(std::clamp(value,0.0,1.0)*255));};
    std::vector<std::uint8_t> rgba(size*size*4,0);
    for (int y=0;y<size;++y) for (int x=0;x<size;++x) {
        const auto distance=std::max(std::abs(x-size/2),std::abs(y-size/2));
        if (distance!=3 && distance!=4) continue;
        const auto color=distance==4?dark:light;auto *pixel=rgba.data()+(y*size+x)*4;
        pixel[0]=byte(color.red);pixel[1]=byte(color.green);pixel[2]=byte(color.blue);pixel[3]=byte(color.alpha);
    }
    auto image=ii::gui::rgba_image(size,size,std::move(rgba));return image?std::move(*image):Image{};
}
}
std::shared_ptr<ii::gui::NativeViewSource> backgroundPickerSurface(std::shared_ptr<BackgroundPanelModel> model,bool rail) {
    return ii::gui::compiled_view_source([model=std::move(model),rail](ii::gui::WidgetBuilder &builder)->ii::core::Result<void> {
        if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Color surface requires a model");
        const auto currentWidth=[&]{return rail?model->railWidth.get():model->planeWidth.get();};
        auto widget=builder.absolute_stack({},rail?"backgroundRailSurface":"backgroundPlaneSurface",{currentWidth(),model->planeHeight.get()});
        if (!widget) return std::unexpected(widget.error());
        auto binding=std::make_shared<Binding>();binding->model=model;binding->widget=*widget;binding->rail=rail;binding->indicator=indicatorImage();
        const std::weak_ptr<Binding> weak=binding;
        if (auto input=widget->set_input_handler([weak](const ii::gui::WidgetInput &event)->ii::core::Result<bool> {
            auto self=weak.lock();if (!self) return false;
            if (event.kind==ii::gui::WidgetInputKind::focus_lost || event.kind==ii::gui::WidgetInputKind::pointer_leave) {
                if (event.kind==ii::gui::WidgetInputKind::focus_lost) self->dragging=false;
                return false;
            }
            if (event.kind==ii::gui::WidgetInputKind::pointer_down) self->dragging=true;
            else if ((event.kind!=ii::gui::WidgetInputKind::pointer_move && event.kind!=ii::gui::WidgetInputKind::pointer_up) || !self->dragging) return false;
            const auto width=self->rail?self->model->railWidth.get():self->model->planeWidth.get();
            const auto height=self->model->planeHeight.get();
            auto changed=self->model->setSurface(self->rail,event.x/std::max(1.0,width),event.y/std::max(1.0,height));
            if (event.kind==ii::gui::WidgetInputKind::pointer_up) self->dragging=false;
            return bool(changed);
        });!input) return input;
        if (auto paint=widget->set_paint_handler([weak](const auto &state)->ii::core::Result<std::shared_ptr<const ii::gui::WidgetDrawing>> {
            auto drawing=std::make_shared<ii::gui::WidgetDrawing>();auto self=weak.lock();
            if (!self) return std::shared_ptr<const ii::gui::WidgetDrawing>(std::move(drawing));
            const auto image=self->rail?self->model->railImage.get():self->model->planeImage.get();
            if (image) drawing->images.push_back({{0,0,float(state.size.width),float(state.size.height)},image});
            if (image && self->indicator) {
                const auto point=self->model->surfacePosition(self->rail);
                const auto size=self->model->markerSize.get();
                const auto x=std::clamp(point.x*state.size.width-size/2,0.0,std::max(0.0,state.size.width-size));
                const auto y=std::clamp(point.y*state.size.height-size/2,0.0,std::max(0.0,state.size.height-size));
                drawing->images.push_back({{float(x),float(y),float(size),float(size)},self->indicator});
            }
            return std::shared_ptr<const ii::gui::WidgetDrawing>(std::move(drawing));
        });!paint) return paint;
        auto repaint=[weak](const auto &,std::uint64_t){if (auto self=weak.lock()) (void)self->widget.request_repaint();};
        binding->image=(rail?model->railImage:model->planeImage).observe(repaint);
        binding->version=model->surfaceVersion.observe(repaint);
        binding->marker=model->markerSize.observe(repaint);
        auto resize=[weak](const auto &,std::uint64_t){if (auto self=weak.lock())
            (void)self->widget.set_extent({self->rail?self->model->railWidth.get():self->model->planeWidth.get(),self->model->planeHeight.get()});};
        binding->width=(rail?model->railWidth:model->planeWidth).observe(resize);binding->height=model->planeHeight.observe(resize);
        return builder.retain(binding);
    });
}
ii::core::Result<void> buildBackgroundPanel(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<BackgroundPanelModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Background panel requires a model");
    auto host=builder.content_host(parent,"backgroundColorHost");if (!host) return std::unexpected(host.error());
    if (auto flex=host->set_flex(1);!flex) return flex;
    return host->set_content([model=std::move(model)](ii::gui::WidgetBuilder &widgets) {
        auto factories=lvui::native::BackendFactories{}.with<NativeBackgroundPanelBackend>([model]{return NativeBackgroundPanelBackend(model);});
        return lvui_generated::NativeBackgroundPanel::build(widgets,{},[](auto &) {},nullptr,factories);
    });
}
std::shared_ptr<ii::gui::NativeViewSource> backgroundPanelSource(std::shared_ptr<BackgroundPanelModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder) {return buildBackgroundPanel(builder,{},model);});
}
}
