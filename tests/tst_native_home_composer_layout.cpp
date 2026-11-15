#include "App/Views/Home/NativeHomeComposerLayout.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class T> T take(ii::core::Result<T> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
    return std::move(*value);
}
void inside(const HomeComposerRect &r, double width, double height) {
    if (!r.visible) return;
    check(r.x >= 0 && r.y >= 0 && r.width > 0 && r.height > 0 && r.x+r.width <= width+0.01 && r.y+r.height <= height+0.01,
          "composer control escaped its grid");
}
template<std::size_t N> void noOverlap(const std::array<HomeComposerRect, N> &items) {
    for (std::size_t a = 0; a < N; ++a) for (std::size_t b = a+1; b < N; ++b) {
        const auto &x = items[a], &y = items[b];
        if (x.visible && y.visible)
            check(x.x+x.width <= y.x+0.001 || y.x+y.width <= x.x+0.001 ||
                  x.y+x.height <= y.y+0.001 || y.y+y.height <= x.y+0.001, "composer controls overlap");
    }
}
void exercise() {
    const HomeComposerLayoutSpec spec{1000,600,400,{88,1,22,93,138,145},{327,88,144,96,120,228,152}};
    const HomeComposerLayoutMetrics metrics{8,12,22,37,44};
    auto wide = take(calculateHomeComposerLayout(1227, 22, false, false, 0, spec, metrics));
    check(wide.contentWidth == 1203 && wide.topHeight == 22 && wide.settingsHeight == 37 && wide.requestedHeight == 91,
          "wide composer geometry differs from the existing layout");
    check(wide.top[1].width > wide.top[0].width && !wide.settings[7].visible && !wide.settings[8].visible,
          "wide prompt did not expand or image view showed video controls");
    for (const auto &item : wide.top) inside(item, wide.contentWidth, wide.topHeight);
    for (const auto &item : wide.settings) inside(item, wide.contentWidth, wide.settingsHeight);
    auto middle = take(calculateHomeComposerLayout(800, 60, true, true, 20, spec, metrics));
    check(middle.topHeight > 3*metrics.controlHeight && middle.settingsHeight == 3*metrics.settingHeight+2*metrics.gap &&
          middle.settings[7].visible && middle.settings[8].visible, "medium video controls were not laid out");
    for (const auto &item : middle.top) inside(item, middle.contentWidth, middle.topHeight);
    for (const auto &item : middle.settings) inside(item, middle.contentWidth, middle.settingsHeight);
    auto narrow = take(calculateHomeComposerLayout(360, 22, true, false, 0, spec, metrics));
    check(narrow.topHeight == 6*metrics.controlHeight+5*metrics.gap && narrow.settings[5].width == narrow.contentWidth &&
          narrow.settings[6].width == narrow.contentWidth && narrow.settings[8].visible, "narrow composer spans or rows are wrong");
    for (const auto &item : narrow.top) inside(item, narrow.contentWidth, narrow.topHeight);
    for (const auto &item : narrow.settings) inside(item, narrow.contentWidth, narrow.settingsHeight);
    for (const auto width : {200., 399., 400., 599., 600., 999., 1000., 1920.}) {
        const auto layout = take(calculateHomeComposerLayout(width, 68, true, true, 40, spec, metrics));
        for (const auto &item : layout.top) inside(item, layout.contentWidth, layout.topHeight);
        for (const auto &item : layout.settings) inside(item, layout.contentWidth, layout.settingsHeight);
        noOverlap(layout.top); noOverlap(layout.settings);
    }
    check(!calculateHomeComposerLayout(std::numeric_limits<double>::quiet_NaN(),22,false,false,0,spec,metrics) &&
          !calculateHomeComposerLayout(320,22,false,false,-1,spec,metrics), "invalid composer geometry was accepted");
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
