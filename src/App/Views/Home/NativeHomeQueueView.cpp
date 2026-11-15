#include "NativeHomeQueueView.h"
#include "NativeHomeQueue.generated.h"
#include <LVUI/NativeImageResolver.h>
#include <iiGUI/SvgImageCache.h>
#include <algorithm>
#include <utility>

namespace dreamscapes::home {
namespace {
constexpr std::string_view indicatorAddress = "asset://dreamscapes/home-queue-ready.svg";
class ReadyIndicator final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    explicit ReadyIndicator(std::shared_ptr<const std::string> source) : source_(std::move(source)) {}
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view address, std::uint32_t width,
                                                     std::uint32_t height) const override {
        if (address != indicatorAddress || width == 0 || height == 0) return {};
        const auto pixels = std::max(width, height);
        const auto value = ii::gui::CachedSvgDecoder::decode(source_, *source_, pixels);
        return value ? *value : nullptr;
    }
  private:
    std::shared_ptr<const std::string> source_;
};
}
ii::core::Result<void> buildHomeQueue(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
                                     std::shared_ptr<HomeQueueModel> model, std::string svg) {
    if (!model || svg.empty()) return ii::core::fail(ii::core::ErrorCode::invalid_argument,
                                                      "Home queue requires a model and ready indicator");
    auto source = std::make_shared<const std::string>(std::move(svg));
    const auto probe = ii::gui::CachedSvgDecoder::decode(source, *source, 6);
    if (!probe) return std::unexpected(probe.error());
    auto resolver = std::make_shared<ReadyIndicator>(std::move(source));
    auto factories = lvui::native::BackendFactories{}.with<NativeHomeQueueBackend>([model] {
        return NativeHomeQueueBackend(model);
    });
    return lvui_generated::NativeHomeQueue::build(widgets, parent, [resolver](auto &scope) {
        (void)scope.image_services->resolver.set(resolver);
    }, nullptr, factories);
}
} // namespace dreamscapes::home
