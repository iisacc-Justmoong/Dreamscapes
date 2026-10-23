#include "AlignmentGeometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace dreamscapes::alignment {
namespace {
constexpr double rad = std::numbers::pi / 180;
bool finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }
Point rotate(Point p, double angle) {
    const auto c = std::cos(angle * rad), s = std::sin(angle * rad);
    return {p.x * c - p.y * s, p.x * s + p.y * c};
}
Point add(Point p, Point q) { return {p.x + q.x, p.y + q.y}; }
Point sub(Point p, Point q) { return {p.x - q.x, p.y - q.y}; }
double dot(Point p, Point q) { return p.x * q.x + p.y * q.y; }
double length(Point p) { return std::hypot(p.x, p.y); }
std::optional<Point> intersection(Line a, Line b) {
    const auto u = sub(a.b, a.a), v = sub(b.b, b.a), w = sub(b.a, a.a);
    const auto cross = [](Point p, Point q) { return p.x * q.y - p.y * q.x; };
    const auto denominator = cross(u, v);
    if (std::abs(denominator) < 1e-10)
        return {};
    const auto t = cross(w, v) / denominator, s = cross(w, u) / denominator;
    if (t < -1e-8 || t > 1 + 1e-8 || s < -1e-8 || s > 1 + 1e-8)
        return {};
    return add(a.a, {u.x * t, u.y * t});
}
std::vector<double> layout(double start, double extent, int count, double gutter, double margin) {
    std::vector<double> out;
    count = std::clamp(count, 1, 512);
    const auto cell = (extent - 2 * margin - (count - 1) * gutter) / count;
    if (cell <= 0 || !std::isfinite(cell))
        return out;
    for (int i = 0; i < count; ++i) {
        out.push_back(start + margin + i * (cell + gutter));
        out.push_back(out.back() + cell);
    }
    return out;
}
int rank(TargetKind kind, int priority) {
    return (priority == 1 && kind == TargetKind::Guide) || (priority == 2 && kind == TargetKind::Grid) ? 0
                                                                                                       : 1;
}
} // namespace
double increment(double value, double step) {
    return std::isfinite(value) && std::isfinite(step) && step > 0 ? std::round(value / step) * step : value;
}
Point closestPoint(Point p, Line l) {
    const auto d = sub(l.b, l.a);
    const auto n = dot(d, d);
    const auto t = n > 0 ? std::clamp(dot(sub(p, l.a), d) / n, 0., 1.) : 0.;
    return {l.a.x + t * d.x, l.a.y + t * d.y};
}
std::optional<Line> clipped(Line l, Rect r) {
    if (!finite(l.a) || !finite(l.b) || !std::isfinite(r.width) || !std::isfinite(r.height) || r.width <= 0 ||
        r.height <= 0)
        return {};
    const auto d = sub(l.b, l.a);
    double t0 = 0, t1 = 1;
    const std::array<double, 4> p{-d.x, d.x, -d.y, d.y},
        q{l.a.x - r.x, r.x + r.width - l.a.x, l.a.y - r.y, r.y + r.height - l.a.y};
    for (int i = 0; i < 4; ++i) {
        if (std::abs(p[i]) < 1e-12) {
            if (q[i] < 0)
                return {};
        } else {
            const auto t = q[i] / p[i];
            if (p[i] < 0)
                t0 = std::max(t0, t);
            else
                t1 = std::min(t1, t);
            if (t0 > t1)
                return {};
        }
    }
    return Line{{l.a.x + t0 * d.x, l.a.y + t0 * d.y}, {l.a.x + t1 * d.x, l.a.y + t1 * d.y}, l.major};
}
std::vector<Line> gridLines(Rect visible, const Grid &g, double zoom, std::size_t limit) {
    std::vector<Line> out;
    if (limit == 0 || visible.width <= 0 || visible.height <= 0 || !std::isfinite(zoom) || zoom <= 0)
        return out;
    out.reserve(std::min<std::size_t>(limit, 512));
    const auto emit = [&](Line l) {
        if (out.size() < limit)
            if (auto c = clipped(l, visible))
                out.push_back(*c);
    };
    const auto ray = [&](Point p, Point direction, bool major = false) {
        const auto span = std::hypot(visible.width, visible.height) + length(sub(p, {visible.x, visible.y}));
        emit({{p.x - direction.x * span, p.y - direction.y * span},
              {p.x + direction.x * span, p.y + direction.y * span},
              major});
    };
    const auto family = [&](double step, double angle, double offset = 0.) {
        if (!std::isfinite(step) || step <= 0)
            return;
        if (g.adaptive)
            while (step * zoom < 4 && step < 1e12)
                step *= 2;
        const auto normal = rotate({1, 0}, angle), dir = rotate({0, 1}, angle);
        double lo = std::numeric_limits<double>::max(), hi = -lo;
        for (const auto p : std::array<Point, 4>{{{visible.x, visible.y},
                                                  {visible.x + visible.width, visible.y},
                                                  {visible.x, visible.y + visible.height},
                                                  {visible.x + visible.width, visible.y + visible.height}}}) {
            const auto v = dot(sub(p, g.origin), normal) - offset;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        const auto start = std::floor(lo / step), end = std::ceil(hi / step);
        // Bounded work even when adaptive display is disabled at a tiny zoom.
        const auto stride = std::max(1., std::ceil((end - start + 1) / std::max<std::size_t>(1, limit / 3)));
        for (double i = start; i <= end && out.size() < limit; i += stride) {
            const auto d = i * step + offset;
            ray({g.origin.x + normal.x * d, g.origin.y + normal.y * d}, dir,
                std::fmod(std::abs(i), std::max(1, g.majorEvery)) < .01);
        }
    };
    const auto subdivisions = std::clamp(g.subdivisions, 1, 128);
    if (g.type == GridType::Rectangular || g.type == GridType::Pixel) {
        family(g.spacingX / (g.type == GridType::Pixel ? 1 : subdivisions), g.rotation);
        family(g.spacingY / (g.type == GridType::Pixel ? 1 : subdivisions), g.rotation + 90);
    } else if (g.type == GridType::Baseline)
        family(g.spacingY, g.rotation + 90, g.baselineOffset);
    else if (g.type == GridType::Isometric) {
        const auto step = g.spacingX / subdivisions;
        family(step * std::cos(g.axisAngle * rad), g.rotation);
        family(step * std::sin(2 * g.axisAngle * rad), g.rotation + 90 - g.axisAngle);
        family(step * std::sin(2 * g.axisAngle * rad), g.rotation + 90 + g.axisAngle);
    } else if (g.type == GridType::Layout) {
        for (const auto x : layout(g.reference.x, g.reference.width, g.columns, g.gutterX, g.marginX))
            emit({{x, g.reference.y}, {x, g.reference.y + g.reference.height}, true});
        for (const auto y : layout(g.reference.y, g.reference.height, g.rows, g.gutterY, g.marginY))
            emit({{g.reference.x, y}, {g.reference.x + g.reference.width, y}, true});
    } else if (g.type == GridType::Polar) {
        const auto step = g.spacingX / subdivisions;
        if (!(step > 0))
            return out;
        int stride = 1;
        if (g.adaptive)
            while (stride < 256 && step * stride * zoom < 4)
                stride *= 2;
        const auto sweep = std::clamp(g.sweep, .1, 360.);
        const int segments = std::clamp(int(sweep / 5), 12, 144);
        for (int ring = stride; ring <= std::clamp(g.ringCount, 1, 256) && out.size() < limit; ring += stride) {
            const auto radius = ring * step;
            for (int i = 0; i < segments && out.size() < limit; ++i) {
                const auto a = (g.startAngle + sweep * i / segments) * rad,
                           b = (g.startAngle + sweep * (i + 1) / segments) * rad;
                emit({{g.origin.x + radius * std::cos(a), g.origin.y + radius * std::sin(a)},
                      {g.origin.x + radius * std::cos(b), g.origin.y + radius * std::sin(b)},
                      ring % std::max(1, g.majorEvery) == 0});
            }
        }
        const auto radius = std::clamp(g.ringCount, 1, 256) * step;
        const auto count = std::clamp(g.spokes, 1, 512);
        for (int i = 0; i < (sweep >= 360 ? count : count + 1); ++i) {
            const auto a = (g.startAngle + sweep * i / count) * rad;
            emit({g.origin,
                  {g.origin.x + radius * std::cos(a), g.origin.y + radius * std::sin(a)},
                  i % std::max(1, g.majorEvery) == 0});
        }
    } else if (g.type == GridType::Perspective) {
        const auto r = g.reference;
        const auto density = std::clamp(g.density, 2, 128);
        for (int i = 0; i <= density; ++i) {
            const Point foot{r.x + r.width * i / density, r.y + r.height};
            emit({g.vanishing1, foot, i % std::max(1, g.majorEvery) == 0});
            if (g.projection > 1)
                emit({g.vanishing2, foot, i % std::max(1, g.majorEvery) == 0});
        }
        const auto horizon = g.vanishing1.y;
        const auto factor = 1 - std::clamp(g.depthStep, .1, 90.) / 100;
        double y = r.y + r.height;
        for (int i = 0; i < 128 && std::abs(y - horizon) * zoom >= 2 && out.size() < limit; ++i) {
            emit({{r.x, y}, {r.x + r.width, y}, i % std::max(1, g.majorEvery) == 0});
            y = horizon + (y - horizon) * factor;
        }
    }
    return out;
}
std::optional<Point> gridPoint(Point p, const Grid &g) {
    if (!finite(p))
        return {};
    const auto local = rotate(sub(p, g.origin), -g.rotation);
    Point next = local;
    const auto n = std::clamp(g.subdivisions, 1, 128);
    if (g.type == GridType::Rectangular || g.type == GridType::Pixel) {
        next = {increment(local.x, g.spacingX / (g.type == GridType::Pixel ? 1 : n)),
                increment(local.y, g.spacingY / (g.type == GridType::Pixel ? 1 : n))};
    } else if (g.type == GridType::Baseline)
        next.y = increment(local.y - g.baselineOffset, g.spacingY) + g.baselineOffset;
    else if (g.type == GridType::Isometric) {
        const auto a = g.axisAngle * rad, s = std::sin(a), c = std::cos(a);
        if (std::abs(s * c) < 1e-6)
            return {};
        const auto u = (local.x / c + local.y / s) / 2, v = (local.x / c - local.y / s) / 2;
        const auto step = g.spacingX / n;
        if (!(step > 0)) return {};
        double best = std::numeric_limits<double>::max();
        for (int i = -1; i <= 1; ++i) for (int j = -1; j <= 1; ++j) {
            const auto x = (std::round(u / step) + i) * step, y = (std::round(v / step) + j) * step;
            const Point candidate{(x + y) * c, (x - y) * s};
            const auto distance = length(sub(candidate, local));
            if (distance < best) { best = distance; next = candidate; }
        }
    } else if (g.type == GridType::Polar) {
        auto a = std::atan2(local.y, local.x) / rad - g.startAngle;
        while (a < 0)
            a += 360;
        const auto sweep = std::clamp(g.sweep, .1, 360.);
        const auto angleStep = sweep / std::clamp(g.spokes, 1, 512), step = g.spacingX / n;
        if (!(step > 0)) return {};
        double best = std::numeric_limits<double>::max();
        for (auto angle : {std::floor(a / angleStep) * angleStep, std::ceil(a / angleStep) * angleStep, 0., sweep}) {
            angle = std::clamp(angle, 0., sweep);
            const auto direction = rotate({1, 0}, g.startAngle + angle);
            const auto radius = std::clamp(increment(dot(local, direction), step), 0., step * std::clamp(g.ringCount, 1, 256));
            const Point candidate{radius * direction.x, radius * direction.y};
            const auto distance = length(sub(candidate, local));
            if (distance < best) { best = distance; next = candidate; }
        }
    } else if (g.type == GridType::Layout) {
        Point result = p;
        const auto xs = layout(g.reference.x, g.reference.width, g.columns, g.gutterX, g.marginX),
                   ys = layout(g.reference.y, g.reference.height, g.rows, g.gutterY, g.marginY);
        if (!xs.empty())
            result.x = *std::min_element(
                xs.begin(), xs.end(), [&](auto a, auto b) { return std::abs(a - p.x) < std::abs(b - p.x); });
        if (!ys.empty())
            result.y = *std::min_element(
                ys.begin(), ys.end(), [&](auto a, auto b) { return std::abs(a - p.y) < std::abs(b - p.y); });
        return result;
    } else { // Perspective intersections use the same finite rays and depth rows.
        const auto lines = gridLines(g.reference, g, 1, 1024);
        double distance = std::numeric_limits<double>::max();
        Point result = p;
        for (std::size_t i = 0; i < lines.size(); ++i)
            for (std::size_t j = i + 1; j < lines.size(); ++j)
                if (const auto candidate = intersection(lines[i], lines[j])) {
                    const auto d = length(sub(*candidate, p));
                    if (d < distance) { distance = d; result = *candidate; }
                }
        if (!std::isfinite(distance) || distance == std::numeric_limits<double>::max()) return {};
        return result;
    }
    return add(g.origin, rotate(next, g.rotation));
}
std::vector<Line> gridSnapLines(Point p, const Grid &g, double radius) {
    std::vector<Line> out;
    if (!finite(p) || !std::isfinite(radius) || radius < 0) return out;
    const auto span = std::max(1., radius * 2);
    const auto family = [&](double step, double angle, double offset = 0.) {
        if (!(step > 0) || !std::isfinite(step)) return;
        const auto normal = rotate({1, 0}, angle), direction = rotate({0, 1}, angle);
        const auto distance = dot(sub(p, g.origin), normal);
        const auto correction = increment(distance - offset, step) + offset - distance;
        const auto q = add(p, {normal.x * correction, normal.y * correction});
        out.push_back({sub(q, {direction.x * span, direction.y * span}),
                       add(q, {direction.x * span, direction.y * span}), true});
    };
    const auto subdivisions = std::clamp(g.subdivisions, 1, 128);
    if (g.type == GridType::Rectangular || g.type == GridType::Pixel) {
        const auto count = g.type == GridType::Pixel ? 1 : subdivisions;
        family(g.spacingX / count, g.rotation);
        family(g.spacingY / count, g.rotation + 90);
    } else if (g.type == GridType::Isometric) {
        const auto step = g.spacingX / subdivisions;
        family(step * std::cos(g.axisAngle * rad), g.rotation);
        family(step * std::sin(2 * g.axisAngle * rad), g.rotation + 90 - g.axisAngle);
        family(step * std::sin(2 * g.axisAngle * rad), g.rotation + 90 + g.axisAngle);
    } else if (g.type == GridType::Baseline)
        family(g.spacingY, g.rotation + 90, g.baselineOffset);
    else if (g.type == GridType::Polar) {
        const auto local = sub(p, g.origin);
        auto angle = std::atan2(local.y, local.x) / rad - g.startAngle;
        while (angle < 0) angle += 360;
        const auto sweep = std::clamp(g.sweep, .1, 360.);
        const auto onSweep = angle <= sweep;
        const auto ring = std::clamp(increment(length(local), g.spacingX / subdivisions),
                                     0., g.spacingX / subdivisions * std::clamp(g.ringCount, 1, 256));
        if (onSweep) {
            const auto a = (g.startAngle + angle) * rad;
            const Point q{g.origin.x + ring * std::cos(a), g.origin.y + ring * std::sin(a)};
            out.push_back({q, q, true});
        }
        const auto addCirclePoint = [&](Point q) {
            auto a = std::atan2(q.y - g.origin.y, q.x - g.origin.x) / rad - g.startAngle;
            while (a < 0) a += 360;
            if (a <= sweep + 1e-8) out.push_back({q, q, true});
        };
        if (std::abs(local.y) <= ring) {
            const auto x = std::sqrt(std::max(0., ring * ring - local.y * local.y));
            addCirclePoint({g.origin.x + x, p.y}); addCirclePoint({g.origin.x - x, p.y});
        }
        if (std::abs(local.x) <= ring) {
            const auto y = std::sqrt(std::max(0., ring * ring - local.x * local.x));
            addCirclePoint({p.x, g.origin.y + y}); addCirclePoint({p.x, g.origin.y - y});
        }
        if (const auto node = gridPoint(p, g)) {
            const auto direction = sub(*node, g.origin);
            const auto n = length(direction);
            if (n > 0) {
                const auto outer = g.spacingX / subdivisions * std::clamp(g.ringCount, 1, 256);
                out.push_back({g.origin, add(g.origin, {direction.x * outer / n, direction.y * outer / n}), true});
            }
        }
    } else {
        auto exact = g; exact.adaptive = false;
        out = gridLines(g.reference, exact, 1, 4096);
    }
    return out;
}
std::vector<GridDot> gridDots(Rect visible, const Grid &g, double zoom, std::size_t limit) {
    std::vector<GridDot> dots;
    if (limit == 0 || !std::isfinite(zoom) || zoom <= 0) return dots;
    const auto emit = [&](Point p, bool major) {
        if (dots.size() < limit && p.x >= visible.x && p.y >= visible.y &&
            p.x <= visible.x + visible.width && p.y <= visible.y + visible.height)
            dots.push_back({p, major});
    };
    if (g.type == GridType::Polar) {
        const auto step = g.spacingX / std::clamp(g.subdivisions, 1, 128);
        if (!(step > 0)) return dots;
        int stride = 1;
        if (g.adaptive) while (stride < 256 && step * stride * zoom < 4) stride *= 2;
        const auto count = std::clamp(g.spokes, 1, 512), rings = std::clamp(g.ringCount, 1, 256);
        const auto sweep = std::clamp(g.sweep, .1, 360.);
        for (int ring = stride; ring <= rings && dots.size() < limit; ring += stride)
            for (int spoke = 0; spoke < count + (sweep < 360); ++spoke) {
                const auto a = (g.startAngle + sweep * spoke / count) * rad;
                emit({g.origin.x + ring * step * std::cos(a), g.origin.y + ring * step * std::sin(a)},
                     ring % std::max(1, g.majorEvery) == 0 || spoke % std::max(1, g.majorEvery) == 0);
            }
    } else {
        auto lattice = g;
        if (g.type == GridType::Baseline) { lattice.type = GridType::Rectangular; lattice.subdivisions = 1; lattice.origin.y += g.baselineOffset; }
        const auto lines = gridLines(visible, lattice, zoom, 512);
        for (std::size_t i = 0; i < lines.size() && dots.size() < limit; ++i)
            for (std::size_t j = i + 1; j < lines.size() && dots.size() < limit; ++j)
                if (const auto p = intersection(lines[i], lines[j]))
                    emit(*p, lines[i].major && lines[j].major);
    }
    const auto key = [](const GridDot &dot) {
        return std::pair{std::round(dot.position.x * 1e6), std::round(dot.position.y * 1e6)};
    };
    std::sort(dots.begin(), dots.end(), [&](const auto &a, const auto &b) {
        return key(a) != key(b) ? key(a) < key(b) : a.major > b.major;
    });
    dots.erase(std::unique(dots.begin(), dots.end(), [&](const auto &a, const auto &b) { return key(a) == key(b); }), dots.end());
    return dots;
}
std::vector<Line> guideLines(Rect r, const Guide &g) {
    std::vector<Line> out;
    const auto x = [&](double v) { out.push_back({{v, r.y}, {v, r.y + r.height}, true}); };
    const auto y = [&](double v) { out.push_back({{r.x, v}, {r.x + r.width, v}, true}); };
    const auto box = [&](double dx, double dy) {
        out.push_back({{r.x + dx, r.y + dy}, {r.x + r.width - dx, r.y + dy}, true});
        out.push_back({{r.x + dx, r.y + r.height - dy}, {r.x + r.width - dx, r.y + r.height - dy}, true});
        out.push_back({{r.x + dx, r.y + dy}, {r.x + dx, r.y + r.height - dy}, true});
        out.push_back({{r.x + r.width - dx, r.y + dy}, {r.x + r.width - dx, r.y + r.height - dy}, true});
    };
    if (r.width <= 0 || r.height <= 0)
        return out;
    if (g.type == GuideType::Single) {
        if (g.orientation == 0)
            y(r.y + g.position);
        else if (g.orientation == 1)
            x(r.x + g.position);
        else {
            const Point center{r.x + r.width / 2, r.y + g.position};
            const auto d = rotate({std::hypot(r.width, r.height) * 2, 0}, g.angle);
            if (auto l = clipped({sub(center, d), add(center, d), true}, r))
                out.push_back(*l);
        }
    } else if (g.type == GuideType::Layout) {
        for (const auto v : layout(r.x, r.width, g.columns, g.gutterX, g.marginX))
            x(v);
        for (const auto v : layout(r.y, r.height, g.rows, g.gutterY, g.marginY))
            y(v);
        if (g.margins)
            box(g.marginX, g.marginY);
    } else if (g.type == GuideType::VideoSafe) {
        if (g.actionSafe)
            box(r.width * g.actionInset / 100, r.height * g.actionInset / 100);
        if (g.titleSafe)
            box(r.width * g.titleInset / 100, r.height * g.titleInset / 100);
    } else if (g.type == GuideType::PrintSafe) {
        box(-g.bleed, -g.bleed);
        box(g.safeMargin, g.safeMargin);
        box(g.cropInset, g.cropInset);
    } else if (g.type == GuideType::Thirds || g.type == GuideType::Golden) {
        const auto f = g.type == GuideType::Thirds ? 1. / 3 : 1. / (1 + std::max(1.001, g.ratio));
        for (const auto t : {f, 1 - f}) {
            x(r.x + r.width * t);
            y(r.y + r.height * t);
        }
    } else {
        const Point center{r.x + r.width / 2, r.y + r.height / 2};
        for (const auto a : {g.angle, -g.angle}) {
            const auto d = rotate({std::hypot(r.width, r.height), 0}, a);
            if (auto l = clipped({sub(center, d), add(center, d), true}, r))
                out.push_back(*l);
        }
    }
    return out;
}
SnapResult snap(std::span<const Point> anchors, std::span<const Target> targets, const SnapSettings &s,
                double zoom, const SnapResult *previous) {
    SnapResult result;
    if (!std::isfinite(zoom) || zoom <= 0 || !std::isfinite(s.tolerance) || !std::isfinite(s.release) || s.tolerance < 0 || s.release < 0)
        return result;
    double bestX = std::numeric_limits<double>::max(), bestY = bestX, bestD = bestX;
    int rankX = 9, rankY = 9, rankD = 9;
    const auto consider = [&](Target t, bool retained) {
        const auto dx = t.line.b.x - t.line.a.x, dy = t.line.b.y - t.line.a.y;
        const bool vertical = std::abs(dx) < 1e-7, horizontal = std::abs(dy) < 1e-7;
        const auto threshold = (retained ? s.release : s.tolerance) / zoom;
        const auto r = retained ? -1 : rank(t.kind, s.priority);
        for (std::size_t anchorIndex = 0; anchorIndex < anchors.size(); ++anchorIndex) {
            if (t.kind == TargetKind::Spacing && anchorIndex > 0)
                break;
            const auto p = anchors[anchorIndex];
            if (!finite(p))
                continue;
            auto q = closestPoint(p, t.line);
            if (!vertical && !horizontal && s.axes != 0) {
                const auto along = s.axes == 1 ? (p.y - t.line.a.y) / dy : (p.x - t.line.a.x) / dx;
                if (along < 0 || along > 1) continue;
                q = add(t.line.a, {dx * along, dy * along});
            }
            const auto correction = sub(q, p);
            if (t.intersection) {
                if ((s.axes == 1 && std::abs(correction.y) > 1e-7) ||
                    (s.axes == 2 && std::abs(correction.x) > 1e-7)) continue;
                if (length(correction) <= threshold &&
                    (r < rankD || (r == rankD && length(correction) < bestD))) {
                    rankD = r;
                    bestD = length(correction);
                    result.diagonal = t;
                }
                continue;
            }
            if (vertical && s.axes != 2 && std::abs(correction.x) <= threshold &&
                (r < rankX || (r == rankX && std::abs(correction.x) < bestX))) {
                rankX = r;
                bestX = std::abs(correction.x);
                result.correction.x = correction.x;
                result.x = t;
            } else if (horizontal && s.axes != 1 && std::abs(correction.y) <= threshold &&
                       (r < rankY || (r == rankY && std::abs(correction.y) < bestY))) {
                rankY = r;
                bestY = std::abs(correction.y);
                result.correction.y = correction.y;
                result.y = t;
            } else if (!vertical && !horizontal && length(correction) <= threshold &&
                       (r < rankD || (r == rankD && length(correction) < bestD))) {
                rankD = r;
                bestD = length(correction);
                result.diagonal = t;
            }
        }
    };
    if (previous) {
        if (previous->x)
            consider(*previous->x, true);
        if (previous->y)
            consider(*previous->y, true);
        if (previous->diagonal)
            consider(*previous->diagonal, true);
    }
    for (const auto &t : targets)
        consider(t, false);
    const auto axisRank = std::min(rankX, rankY);
    if (result.diagonal && (rankD < axisRank || (rankD == axisRank && bestD <= length(result.correction)))) {
        double best = std::numeric_limits<double>::max();
        for (const auto p : anchors) {
            auto q = closestPoint(p, result.diagonal->line);
            const auto &l = result.diagonal->line;
            if (!result.diagonal->intersection && s.axes == 1 && std::abs(l.b.y - l.a.y) > 1e-7)
                q = {l.a.x + (p.y - l.a.y) * (l.b.x - l.a.x) / (l.b.y - l.a.y), p.y};
            else if (!result.diagonal->intersection && s.axes == 2 && std::abs(l.b.x - l.a.x) > 1e-7)
                q = {p.x, l.a.y + (p.x - l.a.x) * (l.b.y - l.a.y) / (l.b.x - l.a.x)};
            const auto d = sub(q, p);
            if (length(d) < best) {
                best = length(d);
                result.correction = d;
            }
        }
        result.x.reset();
        result.y.reset();
    } else
        result.diagonal.reset();
    result.snapped = result.x.has_value() || result.y.has_value() || result.diagonal.has_value();
    return result;
}
} // namespace dreamscapes::alignment
