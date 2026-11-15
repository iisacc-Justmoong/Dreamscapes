#pragma once
#include "NativeReviewGallery.h"
#include <memory>
#include <thread>

namespace dreamscapes::review {
// Detaches only its own resolver before the host tears down its workers.
class ReviewGridImages final {
  public:
    using Resolver=lvui::native::ImageResolver<ii::gui::RasterImage>;
    static std::shared_ptr<ReviewGridImages> create(std::shared_ptr<ReviewGalleryModel>,std::shared_ptr<const Resolver>);
    ~ReviewGridImages();
    void close();
  private:
    ReviewGridImages(std::shared_ptr<ReviewGalleryModel>,std::shared_ptr<const Resolver>);
    const std::thread::id owner_=std::this_thread::get_id();
    std::weak_ptr<ReviewGalleryModel> gallery_;
    std::shared_ptr<const Resolver> resolver_;
    bool active_=true;
};
}
