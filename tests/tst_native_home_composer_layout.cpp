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
    auto compactSpec=spec;
    compactSpec.compactControls=true;
    compactSpec.topPreferred[0]=65;
    compactSpec.topPreferred[5]=71;
    compactSpec.settingsPreferred={67,54,96,83,77,228,152};
    auto compact=take(calculateHomeComposerLayout(1227,22,false,false,0,compactSpec,metrics));
    check(compact.requestedHeight==106 && compact.topHeight==52 && compact.settingsHeight==22 &&
          compact.top[0]==HomeComposerRect{0,0,65,22,true} &&
          compact.top[1]==HomeComposerRect{73,0,1130,22,true} &&
          compact.top[2]==HomeComposerRect{0,30,1203,22,true},
          "QuickGenerate did not match the three rows of Figma 1756:75057");
    check(compact.settings[9]==HomeComposerRect{0,0,22,22,true} &&
          compact.settings[10]==HomeComposerRect{1132,0,71,22,true} &&
          compact.settings[0].x==30 && compact.settings[0].width==67 &&
          compact.settings[4].x==362 && compact.settings[4].width==77 &&
          compact.settings[9].y==compact.settings[10].y && compact.settings[0].y==compact.settings[9].y,
          "compact generation controls did not retain their fixed and content widths");
    check(!compact.settings[5].visible && !compact.settings[6].visible,
          "QuickGenerate rendered settings outside the Figma row");
    auto longerSpec=compactSpec;longerSpec.settingsPreferred[0]+=80;
    auto longer=take(calculateHomeComposerLayout(1227,22,false,false,0,longerSpec,metrics));
    check(longer.settings[0].width==compact.settings[0].width+80 && longer.settings[10].width==71,
          "a longer dropdown label did not change only its natural width");
    const auto video = take(calculateHomeComposerLayout(1227,22,true,false,0,compactSpec,metrics));
    longerSpec.settingsPreferred[1] += 30;
    const auto videoWithLongerLabels = take(calculateHomeComposerLayout(1227,22,true,false,0,longerSpec,metrics));
    check(videoWithLongerLabels.settings[7].width == video.settings[7].width &&
          videoWithLongerLabels.settings[8].width == video.settings[8].width,
          "unrelated labels resized the duration and frame-rate controls");
    for (const auto width:{200.,320.,600.,1227.}) {
        auto wrapped=take(calculateHomeComposerLayout(width,22,true,false,0,compactSpec,metrics));
        check(wrapped.settings[9].width==22 && wrapped.settings[10].width==71 &&
              wrapped.settings[7].visible && wrapped.settings[8].visible,
              "compact actions or video controls changed at a narrow width");
        for (const auto &item:wrapped.top) inside(item,wrapped.contentWidth,wrapped.topHeight);
        for (const auto &item:wrapped.settings) inside(item,wrapped.contentWidth,wrapped.settingsHeight);
        noOverlap(wrapped.top);noOverlap(wrapped.settings);
    }
    check(!calculateHomeComposerLayout(std::numeric_limits<double>::quiet_NaN(),22,false,false,0,spec,metrics) &&
          !calculateHomeComposerLayout(320,22,false,false,-1,spec,metrics), "invalid composer geometry was accepted");
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
