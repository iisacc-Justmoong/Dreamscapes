#pragma once

#include <LVUI/NativeImageResolver.h>
#include <iiGUI/Image.h>
#include <memory>
#include <string_view>

namespace ii::runtime { class EventLoop; class Runtime; }

namespace dreamscapes::review {
// The event loop and workers must outlive this resolver and its pending work.
// File and canvas reads, canvas composition, and image decoding run on workers.
class NativeReviewImages final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    NativeReviewImages(ii::runtime::EventLoop &, ii::runtime::Runtime &);
    ~NativeReviewImages() override;
    NativeReviewImages(const NativeReviewImages &) = delete;
    NativeReviewImages &operator=(const NativeReviewImages &) = delete;

    [[nodiscard]] std::shared_ptr<const ii::gui::RasterImage>
    image(std::string_view source, std::uint32_t width, std::uint32_t height) const override;
    void invalidate(std::string_view source);
    void clear();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dreamscapes::review
