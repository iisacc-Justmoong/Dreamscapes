#include "NativeReviewGrid.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace dreamscapes::review {
namespace {
ReviewGridStrings normalized(ReviewGridStrings value) {
    if (!value.title) value.title=[](std::int64_t ordinal,std::string_view media,std::string_view id) {
        std::size_t end=0,scalars=0;
        while (end<id.size() && scalars<8) {++end; while (end<id.size() && (static_cast<unsigned char>(id[end])&0xc0)==0x80) ++end; ++scalars;}
        std::ostringstream out; out.imbue(std::locale::classic());
        out << std::setfill('0') << std::setw(2) << ordinal << " · " << media << " · "
                                   << (id.empty()?std::string("Unavailable"):std::string(id.substr(0,end)));
        return out.str();
    };
    return value;
}
}
ReviewGridModel::ReviewGridModel(std::shared_ptr<ReviewGalleryModel> gallery,double minimum,ReviewGridStrings text)
    : minimumCellWidth(minimum),strings(normalized(std::move(text))),gallery_(std::move(gallery)) {
    if (!gallery_ || !gallery_->active() || !std::isfinite(minimum) || minimum<1 || minimum>8192)
        throw std::invalid_argument("Review grid requires an active gallery and a valid minimum cell width");
}
std::shared_ptr<ReviewGridModel> ReviewGridModel::create(std::shared_ptr<ReviewGalleryModel> gallery,double minimum,ReviewGridStrings text) {
    return std::shared_ptr<ReviewGridModel>(new ReviewGridModel(std::move(gallery),minimum,std::move(text)));
}
ii::core::Result<void> ReviewGridModel::setViewport(double width,double height) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Review grid requires its owner thread");
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Review grid is closed");
    if (!std::isfinite(width) || !std::isfinite(height) || width<0 || height<0 || width>8192 || height>8192)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Invalid review grid viewport");
    if (generation_==std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ii::core::ErrorCode::out_of_range,"Review grid generation exhausted");
    const auto keepAlive=shared_from_this(); const auto generation=++generation_;
    const auto current=[&]{return active_ && generation_==generation;};
    const auto retired=[] {return ii::core::fail(ii::core::ErrorCode::cancelled,"Review grid changed during layout notification");};
    width=std::max(1.0,width); height=std::max(1.0,height);
    const auto count=static_cast<std::int64_t>(std::max(2.0,std::floor(width/minimumCellWidth)));
    (void)columns.set(count); if (!current()) return retired();
    (void)cellSize.set(width/double(count)); if (!current()) return retired();
    (void)viewportWidth.set(width); if (!current()) return retired();
    (void)viewportHeight.set(height); if (!current()) return retired();
    return {};
}
void ReviewGridModel::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Review grid requires its owner thread");
    if (!active_) return;
    active_=false; gallery_.reset();
}
} // namespace dreamscapes::review
