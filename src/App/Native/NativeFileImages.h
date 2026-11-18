#pragma once

#include "NativePreviewStatus.h"
#include <LVUI>
#include <iiGUI>
#include <memory>
#include <string_view>

namespace ii::runtime { class EventLoop; class Runtime; }

namespace dreamscapes::native {
// The event loop and workers must outlive this resolver and its pending work.
// File and canvas reads, canvas composition, and image decoding run on workers.
class NativeFileImages final : public lvui::native::ImageResolver<ii::gui::RasterImage>, public PreviewStatusSource {
  public:
    NativeFileImages(ii::runtime::EventLoop &, ii::runtime::Runtime &);
    ~NativeFileImages() override;
    NativeFileImages(const NativeFileImages &) = delete;
    NativeFileImages &operator=(const NativeFileImages &) = delete;

    [[nodiscard]] std::shared_ptr<const ii::gui::RasterImage>
    image(std::string_view source, std::uint32_t width, std::uint32_t height) const override;
    [[nodiscard]] PreviewStatus previewStatus(std::string_view source,std::uint32_t width,std::uint32_t height) const override;
    void invalidate(std::string_view source);
    void clear();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dreamscapes::native
