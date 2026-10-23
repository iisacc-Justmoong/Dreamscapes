#include "App/Views/Editor/AlignmentGeometry.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace dreamscapes::alignment;
void check(bool ok, const char *what) {
    if (!ok) {
        std::cerr << what << '\n';
        std::exit(1);
    }
}
int main() {
    const Rect r{0, 0, 1024, 768};
    Grid g;
    g.reference = r;
    for (const auto type : {GridType::Rectangular, GridType::Isometric, GridType::Polar,
                            GridType::Perspective, GridType::Pixel, GridType::Baseline, GridType::Layout}) {
        g.type = type;
        g.origin = {512, 384};
        g.vanishing1 = {-512, 384};
        g.vanishing2 = {1536, 384};
        const auto lines = gridLines(r, g);
        check(!lines.empty(), "All seven grid geometries must produce real lines");
        check(lines.size() <= 4096, "Grid work must be bounded");
        for (const auto l : lines)
            check(std::isfinite(l.a.x) && std::isfinite(l.b.y), "Finite geometry");
        check(gridPoint({377, 248}, g).has_value(), "Every grid supports snapping");
    }
    g.type = GridType::Rectangular;
    g.origin = {0, 0};
    g.subdivisions = 1;
    const auto p = gridPoint({62, 67}, g);
    check(p && p->x == 64 && p->y == 64, "Rectangular intersection snap");
    g.rotation = 90;
    const auto rotated = gridPoint({-62, 67}, g);
    check(rotated && std::abs(rotated->x + 64) < 1e-8 && std::abs(rotated->y - 64) < 1e-8,
          "Rotated grid uses the same coordinate system");
    g.adaptive = false;
    g.spacingX = g.spacingY = .000001;
    check(gridLines({-1e6, -1e6, 2e6, 2e6}, g, .000001, 100).size() <= 100,
          "No unbounded iteration at extreme zoom");
    const std::array<Point, 1> anchor{{{95, 96}}};
    const std::array<Target, 3> targets{{{{{100, -1000}, {100, 1000}}, TargetKind::Geometry},
                                         {{{0, 100}, {1000, 100}}, TargetKind::Geometry},
                                         {{{102, -1000}, {102, 1000}}, TargetKind::Guide}}};
    SnapSettings settings;
    auto s = snap(anchor, targets, settings, 1);
    check(s.snapped && s.correction.x == 5 && s.correction.y == 4, "Snap both axes");
    settings.priority = 1;
    s = snap(anchor, targets, settings, 1);
    check(s.correction.x == 7, "Guide priority");
    settings.axes = 1;
    s = snap(anchor, targets, settings, 1);
    check(s.x && !s.y && s.correction.y == 0, "Horizontal-only movement");
    settings.axes = 0;
    settings.priority = 0;
    s = snap(anchor, targets, settings, 2);
    check(!s.x && s.y, "Tolerance is measured in screen pixels");
    auto held = snap(anchor, targets, settings, 1);
    const std::array<Point, 1> moved{{{90, 90}}};
    s = snap(moved, {}, settings, 1, &held);
    check(s.snapped, "Release radius retains established targets");
    const std::array<Point, 1> released{{{70, 70}}};
    check(!snap(released, {}, settings, 1, &held).snapped, "Release targets outside release radius");
    for (const auto type : {GuideType::Single, GuideType::Layout, GuideType::VideoSafe, GuideType::PrintSafe,
                            GuideType::Thirds, GuideType::Golden, GuideType::Diagonals}) {
        Guide q;
        q.type = type;
        check(!guideLines(r, q).empty(), "All guide types generate geometry");
    }
    Guide safe;
    safe.type = GuideType::VideoSafe;
    const auto lines = guideLines(r, safe);
    check(lines.size() == 8 && std::abs(lines[0].a.x - 51.2) < 1e-8,
          "Safe areas use document-relative percentages");
    check(increment(23, 15) == 30 && increment(.87, .1) > .89, "Rotation and scale increments");
    check(!gridPoint({std::numeric_limits<double>::infinity(), 0}, g), "Reject nonfinite input");
    const std::array<Point, 1> nearPoint{{{98, 96}}};
    const std::array<Target, 1> intersection{{{{{100, 100}, {100, 100}}, TargetKind::Grid, true}}};
    settings.axes = 0;
    settings.priority = 0;
    const auto junction = snap(nearPoint, intersection, settings, 1);
    check(junction.snapped && junction.correction.x == 2 && junction.correction.y == 4,
          "Grid intersections correct both coordinates together");
    const std::array<Point, 1> farPoint{{{98, 80}}};
    check(!snap(farPoint, intersection, settings, 1).snapped,
          "Do not snap one axis to a remote grid intersection");
    const std::array<Target, 1> diagonal{{{{{0, 0}, {1000, 1000}}, TargetKind::Guide}}};
    const std::array<Point, 1> constrained{{{97, 99}}};
    settings.axes = 1;
    auto horizontal = snap(constrained, diagonal, settings, 1);
    check(horizontal.snapped && horizontal.correction.x == 2 && horizontal.correction.y == 0,
          "Horizontal constraint intersects an angled guide without changing Y");
    settings.axes = 2;
    auto vertical = snap(constrained, diagonal, settings, 1);
    check(vertical.snapped && vertical.correction.x == 0 && vertical.correction.y == -2,
          "Vertical constraint intersects an angled guide without changing X");
    settings.axes = 0;
    for (const auto type : {GridType::Rectangular, GridType::Isometric, GridType::Polar,
                           GridType::Perspective, GridType::Pixel, GridType::Baseline, GridType::Layout}) {
        Grid grid; grid.type = type; grid.reference = r; grid.origin = {0, 0};
        grid.vanishing1 = {-512, 384}; grid.vanishing2 = {1536, 384};
        const auto dots = gridDots(r, grid);
        check(!dots.empty() && dots.size() <= 16384, "All seven dot grids render bounded real nodes");
        const auto exact = gridSnapLines({377, 248}, grid, 100);
        check(!exact.empty(), "All seven line grids provide exact snap targets");
    }
    Grid polar; polar.type = GridType::Polar; polar.origin = {512, 384}; polar.reference = r;
    const auto lowZoom = gridLines(r, polar, .01);
    for (const auto line : lowZoom)
        check(std::hypot(line.a.x - 512, line.a.y - 384) <= polar.spacingX * polar.ringCount + 1e-6 &&
              std::hypot(line.b.x - 512, line.b.y - 384) <= polar.spacingX * polar.ringCount + 1e-6,
              "Adaptive polar display cannot enlarge the configured outer ring");
    Grid nearest; nearest.type = GridType::Polar; nearest.spacingX = 100; nearest.subdivisions = 1;
    nearest.spokes = 4; nearest.ringCount = 10;
    const auto polarNode = gridPoint({250, 250}, nearest);
    check(polarNode && std::abs(std::hypot(polarNode->x - 250, polarNode->y - 250) - std::hypot(250., 50.)) < 1e-6,
          "Polar snapping chooses the nearest ring-spoke intersection in document space");
    nearest.type = GridType::Isometric; nearest.axisAngle = 20; nearest.spacingX = 32;
    for (const auto p : {Point{17, 5}, Point{28, 10}, Point{4, 24}}) {
        const auto node = gridPoint(p, nearest); double best = 1e9;
        for (int u = -5; u <= 5; ++u) for (int v = -5; v <= 5; ++v) {
            const auto x = (u + v) * 32 * std::cos(20 * std::acos(-1.) / 180);
            const auto y = (u - v) * 32 * std::sin(20 * std::acos(-1.) / 180);
            best = std::min(best, std::hypot(x - p.x, y - p.y));
        }
        check(node && std::abs(std::hypot(node->x - p.x, node->y - p.y) - best) < 1e-6,
              "Isometric snapping chooses the nearest visible lattice intersection");
    }
    settings.axes = 0; settings.tolerance = 8; settings.release = 0;
    const std::array<Point, 1> releaseProbe{{{99, 300}}};
    const std::array<Target, 1> originalTarget{{{{{100, 0}, {100, 600}}, TargetKind::Geometry}}};
    const std::array<Target, 1> replacementTarget{{{{{98, 0}, {98, 600}}, TargetKind::Geometry}}};
    const auto originalSnap = snap(releaseProbe, originalTarget, settings, 1);
    const auto changedSnap = snap(releaseProbe, replacementTarget, settings, 1, &originalSnap);
    check(changedSnap.correction.x == -1, "Zero release disables retention even when tolerance is larger");
    std::cout << "Alignment geometry contracts passed\n";
}
