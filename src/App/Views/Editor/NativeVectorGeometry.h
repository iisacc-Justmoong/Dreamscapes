#pragma once
#include <Document/Document.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
namespace dreamscapes::editor {
struct VectorBounds final {
    double left=std::numeric_limits<double>::infinity(),top=left,right=-left,bottom=-left;
    void add(iiSharedCanvas::Point p) {left=std::min(left,p.x);top=std::min(top,p.y);right=std::max(right,p.x);bottom=std::max(bottom,p.y);}
    [[nodiscard]] bool valid() const {return std::isfinite(left)&&std::isfinite(top)&&right>left&&bottom>top;}
};
struct VectorGeometry final {VectorBounds box;AffineTransform transform;double width=0,height=0;};
[[nodiscard]] std::optional<VectorGeometry> vectorGeometry(const iiSharedCanvas::Document *,std::string_view layer);
}
