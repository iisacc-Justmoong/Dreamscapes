#include "App/Views/Result/NativeReviewGalleryView.h"
#include <stdexcept>

int main() {
    // Exercise linking the public entry point without opening a native window.
    auto volatile entry = &dreamscapes::review::buildReviewGallery;
    if (entry == nullptr)
        return 1;
    using namespace dreamscapes;
    auto session = std::make_shared<review::Session>();
    auto gallery = review::ReviewGalleryModel::create(session);
    session->replaceEntries({vm::Value(vm::Object{{"id", vm::Value("one")},
                                                  {"documentSource", vm::Value("file:///image.iisc")},
                                                  {"thumbnailSource", vm::Value("file:///image.png")}})});
    auto raster = ii::gui::rgba_image(1, 1, {255, 0, 0, 255});
    if (!raster)
        return 1;
    const auto previous = gallery->items.get().front();
    if (!gallery->setThumbnail(previous->key, previous->source, *raster))
        return 1;
    gallery->bindNavigation(std::make_shared<lvui::native::CollectionNavigation>());
    return gallery->items.get().front()->thumbnail.get() == *raster ? 0 : 1;
}
