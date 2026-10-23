#pragma once
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace dreamscapes::alignment {
struct Point {
    double x = 0, y = 0;
};
struct Rect {
    double x = 0, y = 0, width = 0, height = 0;
};
struct Line {
    Point a, b;
    bool major = false;
};
struct GridDot {
    Point position;
    bool major = false;
};
enum class GridType { Rectangular, Isometric, Polar, Perspective, Pixel, Baseline, Layout };
struct Grid {
    GridType type = GridType::Rectangular;
    Point origin;
    Rect reference;
    double spacingX = 64, spacingY = 64, rotation = 0, axisAngle = 30;
    int subdivisions = 4, majorEvery = 4, spokes = 12, ringCount = 16;
    double startAngle = 0, sweep = 360, baselineOffset = 0;
    int columns = 12, rows = 4;
    double gutterX = 24, gutterY = 24, marginX = 32, marginY = 32;
    Point vanishing1, vanishing2;
    int projection = 2, density = 12;
    double depthStep = 8;
    bool adaptive = true;
};
enum class GuideType { Single, Layout, VideoSafe, PrintSafe, Thirds, Golden, Diagonals };
struct Guide {
    GuideType type = GuideType::Single;
    double position = 320, angle = 45;
    int orientation = 1, columns = 12, rows = 4;
    double gutterX = 24, gutterY = 24, marginX = 32, marginY = 32;
    bool margins = true, actionSafe = true, titleSafe = true;
    double actionInset = 5, titleInset = 10, bleed = 3, safeMargin = 8, cropInset = 0, ratio = 1.618;
};
enum class TargetKind { Geometry, Guide, Grid, Pixel, Spacing };
struct Target {
    Line line;
    TargetKind kind = TargetKind::Geometry;
    bool intersection = false;
};
struct SnapSettings {
    double tolerance = 8, release = 12;
    int priority = 0, axes = 0;
};
struct SnapResult {
    Point correction;
    std::optional<Target> x, y, diagonal;
    bool snapped = false;
};
// Coordinates are document pixels. Detection radii are viewport pixels.
std::vector<Line> gridLines(Rect visible, const Grid &, double zoom = 1, std::size_t limit = 4096);
std::optional<Point> gridPoint(Point, const Grid &);
// Exact nearby lines are independent of adaptive display and viewport density.
std::vector<Line> gridSnapLines(Point, const Grid &, double radius);
std::vector<GridDot> gridDots(Rect visible, const Grid &, double zoom = 1, std::size_t limit = 16384);
std::vector<Line> guideLines(Rect reference, const Guide &);
std::optional<Line> clipped(Line, Rect);
Point closestPoint(Point, Line);
SnapResult snap(std::span<const Point> anchors, std::span<const Target>, const SnapSettings &, double zoom,
                const SnapResult *previous = nullptr);
double increment(double value, double step);
} // namespace dreamscapes::alignment
