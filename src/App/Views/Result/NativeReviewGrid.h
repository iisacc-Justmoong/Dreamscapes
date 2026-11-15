#pragma once
#include "NativeReviewGallery.h"
#include <iiSDKCore/Core/Property/Observable.h>
#include <memory>
#include <thread>

namespace dreamscapes::review {
struct ReviewGridStrings final {
    std::function<std::string(std::int64_t,std::string_view,std::string_view)> title;
    std::string loading="Loading…",unavailable="Preview unavailable";
};
// A responsive projection of an existing session. Geometry is presentation
// state; resizing never changes selected results or their original files.
class ReviewGridModel final : public std::enable_shared_from_this<ReviewGridModel> {
  public:
    static std::shared_ptr<ReviewGridModel> create(std::shared_ptr<ReviewGalleryModel>,double minimumCellWidth,ReviewGridStrings = {});
    [[nodiscard]] ii::core::Result<void> setViewport(double width,double height);
    void close();
    [[nodiscard]] bool active() const noexcept {return active_;}
    [[nodiscard]] std::shared_ptr<ReviewGalleryModel> gallery() const {return gallery_;}
    const double minimumCellWidth;
    const ReviewGridStrings strings;
    ii::core::Observable<double> viewportWidth{1},viewportHeight{1},cellSize{0.5};
    ii::core::Observable<std::int64_t> columns{2};
  private:
    ReviewGridModel(std::shared_ptr<ReviewGalleryModel>,double,ReviewGridStrings);
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<ReviewGalleryModel> gallery_;
    std::uint64_t generation_=0;
    bool active_=true;
};
}
