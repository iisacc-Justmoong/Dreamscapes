#include "NativeBrushSurface.h"
#include <LVUI>
#include <iiGUI>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace dreamscapes::editor {
namespace {
struct Binding final {
    std::shared_ptr<BrushToolModel> model;ii::gui::Widget surface;
    ii::core::ObservableSubscription cursor,width,height;
};
std::shared_ptr<const ii::gui::WidgetDrawing> drawing(const BrushCursor &cursor,ii::gui::Size size) {
    auto result=std::make_shared<ii::gui::WidgetDrawing>();
    if (!cursor.visible || !std::isfinite(cursor.radius) || cursor.radius<=0) return result;
    const auto token=lvui::figma_defaults::text_title_header_color;
    const auto channel=[](double value){return float(lvui::native::linear_color_channel(value));};
    const ii::gui::Color color{channel(token.red),channel(token.green),channel(token.blue),float(token.alpha)};
    const auto count=static_cast<int>(std::clamp(std::ceil(cursor.radius*4),32.0,512.0));
    for (int i=0;i<count;++i) {
        const auto angle=2*std::numbers::pi*i/count;
        const auto x=cursor.x+std::cos(angle)*cursor.radius,y=cursor.y+std::sin(angle)*cursor.radius;
        if (x>=0 && y>=0 && x<size.width && y<size.height)
            result->panels.push_back({{float(x),float(y),1,1},color,color,0});
    }
    return result;
}
}
std::shared_ptr<ii::gui::NativeViewSource> brushSurfaceSource(std::shared_ptr<BrushToolModel> model) {
    return ii::gui::compiled_view_source([model=std::move(model)](ii::gui::WidgetBuilder &builder)->ii::core::Result<void> {
        if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Brush surface requires a model");
        auto surface=builder.absolute_stack({},"editorBrushSurface",{model->width.get(),model->height.get()});
        if (!surface) return std::unexpected(surface.error());
        auto binding=std::make_shared<Binding>();binding->model=model;binding->surface=*surface;
        const std::weak_ptr<Binding> weak=binding;
        if (auto input=surface->set_input_handler([weak](const auto &event)->ii::core::Result<bool> {
            if (auto self=weak.lock()) return self->model->input(event);return false;
        });!input) return input;
        if (auto escape=surface->set_escape_action([weak]{if (auto self=weak.lock()) self->model->cancel();});!escape) return escape;
        if (auto paint=surface->set_paint_handler([weak](const auto &state)->ii::core::Result<std::shared_ptr<const ii::gui::WidgetDrawing>> {
            if (auto self=weak.lock()) return drawing(self->model->cursor.get(),state.size);return std::make_shared<const ii::gui::WidgetDrawing>();
        });!paint) return paint;
        binding->cursor=model->cursor.observe([weak](const auto &,std::uint64_t){if (auto self=weak.lock()) (void)self->surface.request_repaint();});
        auto resize=[weak](const auto &,std::uint64_t){if (auto self=weak.lock())
            (void)self->surface.set_extent({self->model->width.get(),self->model->height.get()});};
        binding->width=model->width.observe(resize);binding->height=model->height.observe(resize);
        if (auto retained=builder.retain(binding);!retained) return retained;
        return builder.on_retire([weak]{if (auto self=weak.lock()) self->model->cancel();});
    });
}
}
