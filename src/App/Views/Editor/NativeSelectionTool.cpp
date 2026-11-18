#include "NativeSelectionTool.h"
#include "NativeEditorFrame.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dreamscapes::editor {
using namespace iiSharedCanvas;using ii::core::ErrorCode;
namespace {
Point world(const AffineTransform &t,Point p) {return {t.m11*p.x+t.m21*p.y+t.translationX,t.m12*p.x+t.m22*p.y+t.translationY};}
Point local(const AffineTransform &t,Point p) {
    const auto x=p.x-t.translationX,y=p.y-t.translationY,d=t.m11*t.m22-t.m12*t.m21;
    return {(t.m22*x-t.m21*y)/d,(-t.m12*x+t.m11*y)/d};
}
std::array<Point,8> handles(const SelectionOverlay &o) {
    const auto &p=o.corners;const auto midpoint=[](Point a,Point b){return Point{(a.x+b.x)/2,(a.y+b.y)/2};};
    return {p[0],midpoint(p[0],p[1]),p[1],midpoint(p[1],p[2]),p[2],midpoint(p[2],p[3]),p[3],midpoint(p[3],p[0])};
}
}
SelectionToolModel::SelectionToolModel(std::shared_ptr<native::DocumentSession> doc):document_(std::move(doc)) {
    if (!document_) throw std::invalid_argument("Select tool requires a document session");
}
std::shared_ptr<SelectionToolModel> SelectionToolModel::create(std::shared_ptr<native::DocumentSession> doc) {
    auto result=std::shared_ptr<SelectionToolModel>(new SelectionToolModel(std::move(doc)));result->start();return result;
}
void SelectionToolModel::start() {
    const std::weak_ptr<SelectionToolModel> weak=shared_from_this();
    auto changed=[weak](const auto &,std::uint64_t){if (auto s=weak.lock()) {s->cancel();s->synchronize();}};
    selected_=document_->selectedLayer.observe(changed);revision_=document_->revision.observe(changed);
    frameObserver_=document_->frame.observe(changed);
    generation_=document_->documentGeneration.observe([weak](const auto &,std::uint64_t){if (auto s=weak.lock()) {
        s->frame_.reset();s->cancel();(void)s->status.set({});s->synchronize();
    }});
}
ii::core::Result<void> SelectionToolModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Select tool requires its owner thread");return {};
}
void SelectionToolModel::setVisible(bool value) {if (!writable()) return;visible_=value;if (!value) cancel();synchronize();}
void SelectionToolModel::setViewport(double w,double h,double size) {
    if (!writable() || !std::isfinite(w) || !std::isfinite(h) || !std::isfinite(size) ||
        w<=0 || h<=0 || w>8192 || h>8192 || size<=0 || size>64) return;
    if (w!=width.get() || h!=height.get() || size!=handleSize_) cancel();
    handleSize_=size;(void)width.set(w);(void)height.set(h);synchronize();
}
void SelectionToolModel::setFrame(std::shared_ptr<const ii::gui::RasterImage> value) {
    if (!writable()) return;
    if (!value || !frame_ || value->width()!=frame_->width() || value->height()!=frame_->height()) cancel();
    frame_=std::move(value);synchronize();
}
std::optional<SelectionToolModel::Mapping> SelectionToolModel::mapping() const {
    if (!visible_ || !frame_ || !frame_->width() || !frame_->height() || !document_->document()) return {};
    const auto region=documentViewRegion(*document_->document());if (region.extent.width<=0 || region.extent.height<=0) return {};
    const auto scale=std::min(width.get()/frame_->width(),height.get()/frame_->height());if (!std::isfinite(scale) || scale<=0) return {};
    const auto w=frame_->width()*scale,h=frame_->height()*scale;return Mapping{(width.get()-w)/2,(height.get()-h)/2,w,h,region};
}
std::optional<VectorGeometry> SelectionToolModel::geometry(std::string_view id) const {
    const auto *doc=document_->document();const auto *layer=doc?findLayer(*doc,std::string(id)):nullptr;
    if (!layer) return {};
    const auto &p=layerProperties(*layer);const auto frame=document_->frame.get();
    if (!p.visible || p.opacity<=0 || p.artboardId ||
        (p.frameRange && (frame<p.frameRange->firstFrame || frame>p.frameRange->lastFrame))) return {};
    return vectorGeometry(doc,id);
}
SelectionOverlay SelectionToolModel::project(const VectorGeometry &g,const Mapping &m) const {
    const auto map=[&](Point p){p=world(g.transform,p);return Point{m.left+(p.x-m.region.origin.x)*m.width/m.region.extent.width,
        m.top+(p.y-m.region.origin.y)*m.height/m.region.extent.height};};
    const auto &b=g.box;SelectionOverlay result{true,{map({b.left,b.top}),map({b.right,b.top}),map({b.right,b.bottom}),map({b.left,b.bottom})},handleSize_};
    if (std::ranges::any_of(result.corners,[](const auto &p){return !std::isfinite(p.x) || !std::isfinite(p.y) ||
        std::abs(p.x)>10000000 || std::abs(p.y)>10000000;})) return {};
    return result;
}
void SelectionToolModel::synchronize() {
    if (!writable()) return;const auto m=mapping();const auto g=document_->hasCurrentLayerSelection()?geometry(document_->selectedLayer.get()):std::nullopt;
    (void)overlay.set(m && g?project(*g,*m):SelectionOverlay{});
}
void SelectionToolModel::cancel() {if (!writable()) return;gesture_.reset();(void)dragging.set(false);synchronize();}
ii::core::Result<bool> SelectionToolModel::input(const ii::gui::WidgetInput &event) {
    if (auto valid=writable();!valid) return std::unexpected(valid.error());
    using Kind=ii::gui::WidgetInputKind;
    if (event.kind==Kind::focus_lost || event.kind==Kind::pointer_cancel || (event.kind==Kind::key_down && event.key==ii::gui::WidgetKey::escape)) {
        const bool had=gesture_.has_value();cancel();return had;
    }
    if (event.local_position && (!std::isfinite(event.local_position->x) || !std::isfinite(event.local_position->y)))
        return ii::core::fail(ErrorCode::invalid_argument,"Selection pointer coordinates must be finite");
    const auto m=mapping();if (!m || !event.local_position) return false;
    const Point position{event.local_position->x,event.local_position->y};
    const auto toWorld=[&](Point p){return Point{m->region.origin.x+(p.x-m->left)*m->region.extent.width/m->width,
        m->region.origin.y+(p.y-m->top)*m->region.extent.height/m->height};};
    if (event.kind==Kind::pointer_down) {
        if (gesture_ || (event.buttons && !(event.buttons&1))) return false;
        auto selected=document_->hasCurrentLayerSelection()?document_->selectedLayer.get():std::string{};
        auto g=geometry(selected);int handle=-1;
        if (g && project(*g,*m).active) {
            const auto points=handles(project(*g,*m));
            for (std::size_t i=0;i<points.size();++i) if (std::abs(position.x-points[i].x)<=handleSize_/2 &&
                std::abs(position.y-points[i].y)<=handleSize_/2) {handle=static_cast<int>(i);break;}
        }
        if (handle<0) {
            selected.clear();const auto point=toWorld(position);const auto *doc=document_->document();
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
            for (auto it=doc->layers.rbegin();it!=doc->layers.rend();++it) {
                const auto &id=layerProperties(*it).id;auto candidate=geometry(id);if (!candidate) continue;
                const auto p=local(candidate->transform,point);const auto &b=candidate->box;
                if (p.x>=b.left && p.x<=b.right && p.y>=b.top && p.y<=b.bottom) {selected=id;g=std::move(candidate);break;}
            }
            if (selected.empty()) {
                if (document_->hasCurrentLayerSelection()) {
                    if (auto cleared=document_->selectLayer({});!cleared) return std::unexpected(cleared.error());
                }
                return true;
            }
            const auto generation=document_->documentGeneration.get(),revision=document_->revision.get();
            const auto frame=document_->frame.get();
            if (auto result=document_->selectLayer(selected);!result) return std::unexpected(result.error());
            if (!visible_ || generation!=document_->documentGeneration.get() || revision!=document_->revision.get() || frame!=document_->frame.get() ||
                !document_->hasCurrentLayerSelection() || document_->selectedLayer.get()!=selected) return true;
        }
        if (!g) return false;
        gesture_=Gesture{selected,document_->documentGeneration.get(),document_->revision.get(),event.device_id,
            document_->frame.get(),handle,toWorld(position),*m,*g,g->transform};
        (void)status.set({});if (!gesture_) return true;(void)dragging.set(true);return true;
    }
    if (!gesture_ || gesture_->device!=event.device_id || (event.kind!=Kind::pointer_move && event.kind!=Kind::pointer_up)) return false;
    auto &g=*gesture_;const auto current=toWorld(position);if (!std::isfinite(current.x) || !std::isfinite(current.y)) {cancel();return true;}
    auto next=g.geometry.transform;
    if (g.handle<0) {next.translationX+=current.x-g.start.x;next.translationY+=current.y-g.start.y;}
    else {
        const auto p=local(g.geometry.transform,current);const auto &b=g.geometry.box;
        static constexpr std::array<int,8> hx{-1,0,1,1,1,0,-1,-1},hy{-1,-1,-1,0,1,1,1,0};
        const auto x=hx[std::size_t(g.handle)],y=hy[std::size_t(g.handle)];
        const auto anchorX=x<0?b.right:b.left,anchorY=y<0?b.bottom:b.top;
        const auto originalX=x<0?b.left:b.right,originalY=y<0?b.top:b.bottom;
        const auto sx=x?std::clamp((p.x-anchorX)/(originalX-anchorX),1/g.geometry.width,65536/g.geometry.width):1;
        const auto sy=y?std::clamp((p.y-anchorY)/(originalY-anchorY),1/g.geometry.height,65536/g.geometry.height):1;
        next.m11*=sx;next.m12*=sx;next.m21*=sy;next.m22*=sy;
        next.translationX+=g.geometry.transform.m11*anchorX*(1-sx)+g.geometry.transform.m21*anchorY*(1-sy);
        next.translationY+=g.geometry.transform.m12*anchorX*(1-sx)+g.geometry.transform.m22*anchorY*(1-sy);
    }
    g.current=next;auto preview=g.geometry;preview.transform=next;(void)overlay.set(project(preview,g.mapping));
    if (!gesture_) return true;
    if (event.kind==Kind::pointer_move) return true;
    const auto complete=*gesture_;gesture_.reset();(void)dragging.set(false);
    if (visible_ && complete.generation==document_->documentGeneration.get() && complete.revision==document_->revision.get() &&
        complete.frame==document_->frame.get() && document_->hasCurrentLayerSelection() && document_->selectedLayer.get()==complete.id) {
        if (auto result=document_->setLayerTransform(complete.id,complete.current);!result) (void)status.set(result.error().message);
    }
    synchronize();return true;
}
}
