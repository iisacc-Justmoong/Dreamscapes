#include "NativeMaskLassoSurface.h"
#include <LVUI>
#include <iiGUI>
#include <algorithm>
#include <cmath>

namespace dreamscapes::editor {
namespace {
struct Binding final {
    std::shared_ptr<MaskLassoModel> model;
    ii::gui::Widget surface;
    ii::core::ObservableSubscription preview,width,height;
};
std::shared_ptr<const ii::gui::WidgetDrawing> drawing(const MaskLassoPreview &preview,ii::gui::Size size) {
    auto result=std::make_shared<ii::gui::WidgetDrawing>();if (!preview.active || preview.points.size()<2) return result;
    const auto token=lvui::figma_defaults::accent_blue_default;
    const auto channel=[](double value){return float(lvui::native::linear_color_channel(value));};
    const ii::gui::Color color{channel(token.red),channel(token.green),channel(token.blue),float(token.alpha)};
    for (std::size_t i=1;i<preview.points.size();++i) {
        const auto a=preview.points[i-1],b=preview.points[i];const auto dx=b.x-a.x,dy=b.y-a.y;
        const auto length=std::hypot(dx,dy);if (!std::isfinite(length) || length<=0) continue;
        const auto steps=int(std::clamp(std::ceil(length/4),1.0,2048.0));
        for (int j=0;j<steps;j+=2) {
            if (result->panels.size()>=8192) return result;
            const auto start=double(j)/steps,end=double(std::min(j+1,steps))/steps;
            const auto x1=std::clamp(a.x+dx*start,0.0,size.width),x2=std::clamp(a.x+dx*end,0.0,size.width);
            const auto y1=std::clamp(a.y+dy*start,0.0,size.height),y2=std::clamp(a.y+dy*end,0.0,size.height);
            const auto w=std::max(1.0,std::abs(x2-x1)),h=std::max(1.0,std::abs(y2-y1));
            result->panels.push_back({{float(std::min(x1,x2)),float(std::min(y1,y2)),float(w),float(h)},color,color,0});
        }
    }
    return result;
}
}
std::shared_ptr<ii::gui::NativeViewSource> maskLassoSurfaceSource(std::shared_ptr<MaskLassoModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder)->ii::core::Result<void> {
        if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Mask lasso surface requires a model");
        auto surface=builder.absolute_stack({},"editorMaskLassoSurface",{model->width.get(),model->height.get()});
        if (!surface) return std::unexpected(surface.error());
        auto binding=std::make_shared<Binding>();binding->model=model;binding->surface=*surface;const std::weak_ptr<Binding> weak=binding;
        if (auto escape=surface->set_escape_action([weak]{if (auto self=weak.lock()) self->model->cancel();});!escape) return escape;
        if (auto input=surface->set_input_handler([weak](const auto &event)->ii::core::Result<bool> {
            if (auto self=weak.lock()) return self->model->input(event);return false;
        });!input) return input;
        if (auto paint=surface->set_paint_handler([weak](const auto &state)->ii::core::Result<std::shared_ptr<const ii::gui::WidgetDrawing>> {
            if (auto self=weak.lock()) return drawing(self->model->preview.get(),state.size);return std::make_shared<const ii::gui::WidgetDrawing>();
        });!paint) return paint;
        binding->preview=model->preview.observe([weak](const auto &,std::uint64_t){if (auto self=weak.lock()) (void)self->surface.request_repaint();});
        const auto resize=[weak](const auto &,std::uint64_t){if (auto self=weak.lock())
            (void)self->surface.set_extent({self->model->width.get(),self->model->height.get()});};
        binding->width=model->width.observe(resize);binding->height=model->height.observe(resize);
        if (auto retained=builder.retain(binding);!retained) return retained;
        return builder.on_retire([weak]{if (auto self=weak.lock()) self->model->cancel();});
    });
}
}
