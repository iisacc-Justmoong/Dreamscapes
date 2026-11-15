#include "NativeReviewGridImages.h"
#include <stdexcept>
#include <utility>

namespace dreamscapes::review {
ReviewGridImages::ReviewGridImages(std::shared_ptr<ReviewGalleryModel> gallery,std::shared_ptr<const Resolver> resolver)
    : gallery_(gallery),resolver_(std::move(resolver)) {
    if (!gallery || !gallery->active() || !resolver_) throw std::invalid_argument("Grid images require an active gallery and resolver");
}
std::shared_ptr<ReviewGridImages> ReviewGridImages::create(std::shared_ptr<ReviewGalleryModel> gallery,std::shared_ptr<const Resolver> resolver) {
    auto binding=std::shared_ptr<ReviewGridImages>(new ReviewGridImages(gallery,std::move(resolver)));
    gallery->setImageResolver(binding->resolver_);
    if (!gallery->active()) {binding->close(); throw std::logic_error("Gallery retired while attaching images");}
    return binding;
}
ReviewGridImages::~ReviewGridImages() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
void ReviewGridImages::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Grid images require their owner thread");
    if (!active_) return;
    active_=false; auto resolver=std::move(resolver_); auto gallery=gallery_.lock(); gallery_.reset();
    if (gallery) (void)gallery->clearImageResolver(resolver);
}
} // namespace dreamscapes::review
