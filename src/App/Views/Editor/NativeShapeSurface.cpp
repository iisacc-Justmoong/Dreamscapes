#include "NativeShapeSurface.h"
#include <LVUI>
#include <iiGUI>
#include <algorithm>
#include <cmath>

namespace dreamscapes::editor {
namespace {
struct Binding final {
    std::shared_ptr<ShapeToolModel> model;
    ii::gui::Widget surface;
    ii::core::ObservableSubscription preview,width,height;
};
std::shared_ptr<const ii::gui::WidgetDrawing> drawing(const ShapePreview &preview,ii::gui::Size size) {
    auto result=std::make_shared<ii::gui::WidgetDrawing>();
    if (!preview.active) return result;
    const auto token=lvui::figma_defaults::accent_blue_default;
    const auto channel=[](double value){return static_cast<float>(lvui::native::linear_color_channel(value));};
    const ii::gui::Color color{channel(token.red),channel(token.green),channel(token.blue),static_cast<float>(token.alpha)};
    const auto add=[&](double x,double y,double w,double h) {
        const auto x1=std::clamp(x,0.0,size.width),y1=std::clamp(y,0.0,size.height);
        const auto x2=std::clamp(x+w,0.0,size.width),y2=std::clamp(y+h,0.0,size.height);
        if (x2>x1 && y2>y1) result->panels.push_back({{float(x1),float(y1),float(x2-x1),float(y2-y1)},color,color,0});
    };
    const auto left=std::min(preview.x1,preview.x2),right=std::max(preview.x1,preview.x2);
    const auto top=std::min(preview.y1,preview.y2),bottom=std::max(preview.y1,preview.y2);
    const auto step=std::max({8.0,(right-left)/128,(bottom-top)/128}),dash=step/2;
    for (double x=left;x<right;x+=step) {const auto length=std::min(dash,right-x);add(x,top,length,1);add(x,bottom,length,1);}
    for (double y=top;y<bottom;y+=step) {const auto length=std::min(dash,bottom-y);add(left,y,1,length);add(right,y,1,length);}
    return result;
}
}
std::shared_ptr<ii::gui::NativeViewSource> shapeSurfaceSource(std::shared_ptr<ShapeToolModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder)->ii::core::Result<void> {
        if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Shape surface requires a model");
        auto surface=builder.absolute_stack({},"editorShapeSurface",{model->width.get(),model->height.get()});
        if (!surface) return std::unexpected(surface.error());
        auto binding=std::make_shared<Binding>();binding->model=model;binding->surface=*surface;
        const std::weak_ptr<Binding> weak=binding;
        if (auto input=surface->set_input_handler([weak](const auto &event)->ii::core::Result<bool> {
            if (auto self=weak.lock()) return self->model->input(event);return false;
        });!input) return input;
        if (auto paint=surface->set_paint_handler([weak](const auto &state)->ii::core::Result<std::shared_ptr<const ii::gui::WidgetDrawing>> {
            if (auto self=weak.lock()) return drawing(self->model->preview.get(),state.size);
            return std::make_shared<const ii::gui::WidgetDrawing>();
        });!paint) return paint;
        binding->preview=model->preview.observe([weak](const auto &,std::uint64_t) {
            if (auto self=weak.lock()) (void)self->surface.request_repaint();
        });
        auto resize=[weak](const auto &,std::uint64_t) {
            if (auto self=weak.lock()) (void)self->surface.set_extent({self->model->width.get(),self->model->height.get()});
        };
        binding->width=model->width.observe(resize);binding->height=model->height.observe(resize);
        if (auto retained=builder.retain(binding);!retained) return retained;
        return builder.on_retire([weak]{if (auto self=weak.lock()) self->model->cancel();});
    });
}
}
