#include "NativeVectorGeometry.h"
#include <algorithm>
#include <type_traits>
namespace dreamscapes::editor {
using namespace iiSharedCanvas;
namespace {
Point quadratic(Point a,Point b,Point c,double t) {
    const auto u=1-t;return {u*u*a.x+2*u*t*b.x+t*t*c.x,u*u*a.y+2*u*t*b.y+t*t*c.y};
}
Point cubic(Point a,Point b,Point c,Point d,double t) {
    const auto u=1-t;return {u*u*u*a.x+3*u*u*t*b.x+3*u*t*t*c.x+t*t*t*d.x,
                             u*u*u*a.y+3*u*u*t*b.y+3*u*t*t*c.y+t*t*t*d.y};
}
template<class F> void quadraticExtrema(double a,double b,double c,F include) {
    const auto denominator=a-2*b+c;
    if (denominator!=0) {const auto t=(a-b)/denominator;if (t>0 && t<1) include(t);}
}
template<class F> void cubicExtrema(double a,double b,double c,double d,F include) {
    const auto A=-a+3*b-3*c+d,B=2*(a-2*b+c),C=b-a;
    auto add=[&](double t) {if (std::isfinite(t) && t>0 && t<1) include(t);};
    if (A==0) {if (B!=0) add(-C/B);return;}
    const auto discriminant=std::fma(-4*A,C,B*B);
    if (discriminant<0 || !std::isfinite(discriminant)) return;
    const auto root=std::sqrt(discriminant);
    const auto q=-.5*(B+std::copysign(root,B));
    if (q!=0) {add(q/A);add(C/q);}
    else add(-B/(2*A));
}
std::optional<VectorBounds> bounds(const VectorAsset &asset) {
    VectorBounds box;
    for (const auto &path:asset.paths) {
        Point previous{},first{};bool started=false;
        for (const auto &command:path.commands) std::visit([&](const auto &item) {
            using T=std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T,MoveTo>) {previous=first=item.point;started=true;}
            else if constexpr (std::is_same_v<T,LineTo>) {if (started) {box.add(previous);box.add(item.point);previous=item.point;}}
            else if constexpr (std::is_same_v<T,QuadraticTo>) {
                if (!started) return;
                box.add(previous);box.add(item.end);
                const auto include=[&](double t){box.add(quadratic(previous,item.control,item.end,t));};
                quadraticExtrema(previous.x,item.control.x,item.end.x,include);
                quadraticExtrema(previous.y,item.control.y,item.end.y,include);
                previous=item.end;
            } else if constexpr (std::is_same_v<T,CubicTo>) {
                if (!started) return;
                box.add(previous);box.add(item.end);
                const auto include=[&](double t){box.add(cubic(previous,item.control1,item.control2,item.end,t));};
                cubicExtrema(previous.x,item.control1.x,item.control2.x,item.end.x,include);
                cubicExtrema(previous.y,item.control1.y,item.control2.y,item.end.y,include);
                previous=item.end;
            } else if constexpr (std::is_same_v<T,ClosePath>) {
                if (started) {
                    if (previous.x!=first.x || previous.y!=first.y) {box.add(previous);box.add(first);}
                    previous=first;
                }
            }
        },command);
    }
    if (!box.valid()) return std::nullopt;
    return box;
}
} // namespace
std::optional<VectorGeometry> vectorGeometry(const Document *document,std::string_view id) {
    if (!document) return std::nullopt;
    const auto *layer=findLayer(*document,std::string(id));
    const auto *vector=layer?std::get_if<StaticVectorLayer>(layer):nullptr;
    if (!vector || !vector->properties.motion.empty()) return std::nullopt;
    const auto *asset=findVectorAsset(*document,vector->content.assetId);
    if (!asset) return std::nullopt;
    auto box=bounds(*asset);if (!box) return std::nullopt;
    const auto t=vector->properties.transform;
    const auto determinant=t.m11*t.m22-t.m12*t.m21;
    if (!std::isfinite(determinant)||std::abs(determinant)<1e-12) return std::nullopt;
    const auto width=(box->right-box->left)*std::hypot(t.m11,t.m12);
    const auto height=(box->bottom-box->top)*std::hypot(t.m21,t.m22);
    if (!std::isfinite(width)||!std::isfinite(height)||width<=1e-9||height<=1e-9) return std::nullopt;
    return VectorGeometry{*box,t,width,height};
}

} // namespace dreamscapes::editor
