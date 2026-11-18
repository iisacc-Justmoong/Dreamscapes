#include "NativeSelectionSurface.h"
#include <LVUI>
#include <iiGUI>
#include <algorithm>
#include <cmath>

namespace dreamscapes::editor {
namespace {
struct Binding final {
    std::shared_ptr<SelectionToolModel> model;
    ii::gui::Widget surface;
    ii::core::ObservableSubscription overlay,width,height;
};
std::shared_ptr<const ii::gui::WidgetDrawing> drawing(const SelectionOverlay &overlay,ii::gui::Size size) {
    auto result=std::make_shared<ii::gui::WidgetDrawing>();if (!overlay.active) return result;
    const auto convert=[](const auto &token) {
        const auto channel=[](double value){return static_cast<float>(lvui::native::linear_color_channel(value));};
        return ii::gui::Color{channel(token.red),channel(token.green),channel(token.blue),float(token.alpha)};
    };
    const auto blue=convert(lvui::figma_defaults::accent_blue_default);
    const auto surface=convert(lvui::figma_defaults::panel_surface_solid);
    const auto add=[&](double x,double y,double w,double h,ii::gui::Color color) {
        const auto left=std::clamp(x,0.0,size.width),top=std::clamp(y,0.0,size.height);
        const auto right=std::clamp(x+w,0.0,size.width),bottom=std::clamp(y+h,0.0,size.height);
        if (right>left && bottom>top) result->panels.push_back({{float(left),float(top),float(right-left),float(bottom-top)},color,color,0});
    };
    const auto &p=overlay.corners;
    for (int edge=0;edge<4;++edge) {
        const auto a=p[edge],b=p[(edge+1)%4];const auto dx=b.x-a.x,dy=b.y-a.y;
        if (std::abs(dx)<.001) {add(std::min(a.x,b.x),std::min(a.y,b.y),1,std::max(1.0,std::abs(dy)),blue);continue;}
        if (std::abs(dy)<.001) {add(std::min(a.x,b.x),std::min(a.y,b.y),std::max(1.0,std::abs(dx)),1,blue);continue;}
        const auto steps=int(std::clamp(std::ceil(std::max(std::abs(dx),std::abs(dy))),1.0,2048.0));
        for (int i=0;i<steps;++i) {
            const auto t=double(i)/steps,u=double(i+1)/steps,x1=a.x+dx*t,y1=a.y+dy*t,x2=a.x+dx*u,y2=a.y+dy*u;
            add(std::min(x1,x2),std::min(y1,y2),std::max(1.0,std::abs(x2-x1)),std::max(1.0,std::abs(y2-y1)),blue);
        }
    }
    const auto mid=[](auto a,auto b){return iiSharedCanvas::Point{(a.x+b.x)/2,(a.y+b.y)/2};};
    const std::array points{p[0],mid(p[0],p[1]),p[1],mid(p[1],p[2]),p[2],mid(p[2],p[3]),p[3],mid(p[3],p[0])};
    for (const auto point:points) {
        const auto x=point.x-overlay.handleSize/2,y=point.y-overlay.handleSize/2;
        add(x,y,overlay.handleSize,overlay.handleSize,blue);
        add(x+1,y+1,std::max(0.0,overlay.handleSize-2),std::max(0.0,overlay.handleSize-2),surface);
    }
    return result;
}
}
std::shared_ptr<ii::gui::NativeViewSource> selectionSurfaceSource(std::shared_ptr<SelectionToolModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder)->ii::core::Result<void> {
        if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Selection surface requires a model");
        auto surface=builder.absolute_stack({},"editorSelectionSurface",{model->width.get(),model->height.get()});
        if (!surface) return std::unexpected(surface.error());
        auto binding=std::make_shared<Binding>();binding->model=model;binding->surface=*surface;const std::weak_ptr<Binding> weak=binding;
        if (auto escape=surface->set_escape_action([weak]{if (auto self=weak.lock()) self->model->cancel();});!escape) return escape;
        if (auto input=surface->set_input_handler([weak](const auto &event)->ii::core::Result<bool> {
            if (auto self=weak.lock()) return self->model->input(event);return false;
        });!input) return input;
        if (auto paint=surface->set_paint_handler([weak](const auto &state)->ii::core::Result<std::shared_ptr<const ii::gui::WidgetDrawing>> {
            if (auto self=weak.lock()) return drawing(self->model->overlay.get(),state.size);return std::make_shared<const ii::gui::WidgetDrawing>();
        });!paint) return paint;
        binding->overlay=model->overlay.observe([weak](const auto &,std::uint64_t){if (auto self=weak.lock()) (void)self->surface.request_repaint();});
        const auto resize=[weak](const auto &,std::uint64_t){if (auto self=weak.lock())
            (void)self->surface.set_extent({self->model->width.get(),self->model->height.get()});};
        binding->width=model->width.observe(resize);binding->height=model->height.observe(resize);
        if (auto retained=builder.retain(binding);!retained) return retained;
        return builder.on_retire([weak]{if (auto self=weak.lock()) self->model->cancel();});
    });
}
}
