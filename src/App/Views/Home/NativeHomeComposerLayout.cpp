#include "NativeHomeComposerLayout.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace dreamscapes::home {
namespace {
bool dimension(double value) { return std::isfinite(value) && value >= 0 && value <= 8192; }
template<std::size_t N> bool preferences(const std::array<double, N> &values) {
    return std::ranges::all_of(values, [](double value) { return dimension(value) && value > 0; });
}
}
ii::core::Result<HomeComposerLayout> calculateHomeComposerLayout(double width, double promptHeight, bool video,
        bool attachments, double errorHeight, const HomeComposerLayoutSpec &spec, const HomeComposerLayoutMetrics &metrics) {
    using ii::core::ErrorCode;
    if (!dimension(width) || !dimension(promptHeight) || !dimension(errorHeight) ||
        !dimension(spec.wideBreakpoint) || !dimension(spec.mediumBreakpoint) || !dimension(spec.compactBreakpoint) ||
        spec.compactBreakpoint > spec.mediumBreakpoint || spec.mediumBreakpoint > spec.wideBreakpoint ||
        !preferences(spec.topPreferred) || !preferences(spec.settingsPreferred) ||
        !dimension(metrics.gap) || !dimension(metrics.inset) || !dimension(metrics.controlHeight) ||
        !dimension(metrics.settingHeight) || !dimension(metrics.attachmentHeight) || metrics.controlHeight <= 0 ||
        metrics.settingHeight < metrics.controlHeight || (attachments && metrics.attachmentHeight <= 0) || width <= 2*metrics.inset)
        return ii::core::fail(ErrorCode::invalid_argument, "Invalid Home composer layout inputs");
    HomeComposerLayout result;
    result.contentWidth = width - 2*metrics.inset;
    const auto gap = metrics.gap;
    const auto prompt = std::max(metrics.controlHeight, promptHeight);
    if (width >= spec.wideBreakpoint) {
        double fixed = 0;
        for (std::size_t i = 0; i < result.top.size(); ++i) if (i != 1) fixed += spec.topPreferred[i];
        const auto room = result.contentWidth - gap * (result.top.size()-1);
        if (room <= fixed) return ii::core::fail(ErrorCode::out_of_range, "Composer width cannot fit its desktop controls");
        result.topHeight = prompt;
        double x = 0;
        for (std::size_t i = 0; i < result.top.size(); ++i) {
            const auto itemWidth = i == 1 ? room-fixed : spec.topPreferred[i];
            result.top[i] = {x, 0, itemWidth, result.topHeight, true};
            x += itemWidth + gap;
        }
        const auto settingsRoom = result.contentWidth - gap * (spec.settingsPreferred.size()-1);
        if (settingsRoom <= 0) return ii::core::fail(ErrorCode::out_of_range, "Composer width cannot fit desktop settings");
        const auto total = std::accumulate(spec.settingsPreferred.begin(), spec.settingsPreferred.end(), 0.0);
        x = 0;
        for (std::size_t i = 0; i < spec.settingsPreferred.size(); ++i) {
            const auto itemWidth = settingsRoom * spec.settingsPreferred[i] / total;
            result.settings[i] = {x, 0, itemWidth, metrics.settingHeight, true}; x += itemWidth + gap;
        }
        result.settingsHeight = metrics.settingHeight;
        if (video) {
            result.settings[7] = {0, metrics.settingHeight+gap, result.settings[0].width, metrics.settingHeight, true};
            result.settings[8] = {result.settings[1].x, metrics.settingHeight+gap, result.settings[1].width, metrics.settingHeight, true};
            result.settingsHeight += gap + metrics.settingHeight;
        } else { result.settings[7].visible = false; result.settings[8].visible = false; }
    } else {
        const std::size_t columns = width < spec.compactBreakpoint ? 1 : 2;
        const auto cell = (result.contentWidth-gap*(columns-1))/columns;
        if (cell <= 0) return ii::core::fail(ErrorCode::out_of_range, "Composer width cannot fit input controls");
        double y = 0;
        for (std::size_t first = 0; first < result.top.size(); first += columns) {
            const auto end = std::min(first+columns, result.top.size());
            const auto rowHeight = first <= 1 && 1 < end ? prompt : metrics.controlHeight;
            for (std::size_t i = first; i < end; ++i)
                result.top[i] = {(i-first)*(cell+gap), y, cell, rowHeight, true};
            y += rowHeight + (end < result.top.size() ? gap : 0);
        }
        result.topHeight = y;
        const std::size_t settingsColumns = width >= spec.mediumBreakpoint ? 3 : 2;
        const auto settingCell = (result.contentWidth-gap*(settingsColumns-1))/settingsColumns;
        if (settingCell <= 0) return ii::core::fail(ErrorCode::out_of_range, "Composer width cannot fit settings");
        std::size_t column = 0, row = 0;
        const auto count = video ? result.settings.size() : spec.settingsPreferred.size();
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t span = width < spec.mediumBreakpoint && (i == 5 || i == 6) ? 2 : 1;
            if (column + span > settingsColumns) { column = 0; ++row; }
            result.settings[i] = {column*(settingCell+gap), row*(metrics.settingHeight+gap),
                                  span*settingCell+(span-1)*gap, metrics.settingHeight, true};
            column += span;
            if (column == settingsColumns && i+1 < count) { column = 0; ++row; }
        }
        result.settingsHeight = (row+1)*metrics.settingHeight + row*gap;
        if (!video) { result.settings[7].visible = false; result.settings[8].visible = false; }
    }
    result.requestedHeight = 2*metrics.inset + result.topHeight + gap + result.settingsHeight +
        (attachments ? gap+metrics.attachmentHeight : 0) + (errorHeight > 0 ? gap+errorHeight : 0);
    if (!dimension(result.requestedHeight)) return ii::core::fail(ErrorCode::out_of_range, "Home composer exceeds its layout limit");
    return result;
}
} // namespace dreamscapes::home
