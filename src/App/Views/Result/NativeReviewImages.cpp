#include "NativeReviewImages.h"
#include <NativeImageLoader.h>
#include <Render/DocumentPreview.h>
#include <iiFileProviderNative.h>
#include <iiNetwork/Wire.h>
#include <iiSDKCore/Core/Text/Utf8.h>
#include <iiVisualCodec/ImageCodec.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dreamscapes::review {
namespace {
using ii::core::ErrorCode;
constexpr std::string_view canvasPrefix = "image://iisc-preview/";

bool canvasPath(const std::filesystem::path &path) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".iisc";
}

std::optional<std::string> loaderAddress(std::string_view address) {
    if (!address.starts_with(canvasPrefix)) return std::string(address);
    if (address.size() > 8192) return {};
    auto encoded = address.substr(canvasPrefix.size());
    const auto suffix = encoded.find_first_of("?#");
    const auto revision = suffix == encoded.npos ? std::string_view{} : encoded.substr(suffix);
    if (suffix != encoded.npos) encoded = encoded.substr(0, suffix);
    auto decoded = ii::network::wire::unbase64(encoded, true);
    if (!decoded || decoded->empty() || decoded->find('\0') != decoded->npos || !ii::core::valid_utf8(*decoded)) return {};
    std::u8string utf8;
    utf8.reserve(decoded->size());
    for (const unsigned char c : *decoded) utf8.push_back(static_cast<char8_t>(c));
    const auto path = std::filesystem::path(utf8);
    if (!path.is_absolute() || !canvasPath(path)) return {};
    const auto generic = path.generic_u8string();
    const std::string text(generic.begin(), generic.end());
    std::string result = "file://";
#if defined(_WIN32)
    if (!text.starts_with('/')) result.push_back('/');
#endif
    static constexpr char hex[] = "0123456789ABCDEF";
    for (const auto c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '/' || byte == ':' || byte == '-' ||
            byte == '_' || byte == '.' || byte == '~') result.push_back(static_cast<char>(byte));
        else { result.push_back('%'); result.push_back(hex[byte >> 4]); result.push_back(hex[byte & 15]); }
    }
    result.append(revision);
    return result;
}

class CanvasFileReader final : public lvui::native::ImageFileReader {
  public:
    ii::core::Result<std::string> read(const std::filesystem::path &path, std::size_t maximum) override {
        try {
            if (!canvasPath(path)) return iiFileProvider::native::File::read(path, maximum);
            const auto pathBytes = path.u8string();
            auto preview = iiSharedCanvas::readIiscPreview(std::string(pathBytes.begin(), pathBytes.end()));
            if (!preview.ok()) return ii::core::fail(ErrorCode::failed, preview.result.message);
            const auto &source = preview.pixels;
            std::vector<std::uint8_t> rgba;
            rgba.reserve(source.pixels.size() * 4);
            for (const auto pixel : source.pixels) {
                rgba.push_back(static_cast<std::uint8_t>(pixel >> 16));
                rgba.push_back(static_cast<std::uint8_t>(pixel >> 8));
                rgba.push_back(static_cast<std::uint8_t>(pixel));
                rgba.push_back(static_cast<std::uint8_t>(pixel >> 24));
            }
            auto bytes = ii::visual::image::encode_png({static_cast<std::uint32_t>(source.width),
                static_cast<std::uint32_t>(source.height), ii::visual::image::PixelFormat::rgba8, 0, rgba});
            if (!bytes) return ii::core::fail(ErrorCode::failed, bytes.error().message);
            if (bytes->size() > maximum) return ii::core::fail(ErrorCode::full, "Canvas preview exceeds the image byte limit");
            return std::string(reinterpret_cast<const char *>(bytes->data()), bytes->size());
        } catch (const std::exception &error) {
            return ii::core::fail(ErrorCode::failed, error.what(), std::current_exception());
        }
    }
};
} // namespace

class NativeReviewImages::Impl final {
  public:
    lvui::native::ImageLoader loader;
    Impl(ii::runtime::EventLoop &loop, ii::runtime::Runtime &workers)
        : loader(loop, workers, lvui::native::ImageSourceOptions{.file_reader = std::make_shared<CanvasFileReader>()}) {}
};

NativeReviewImages::NativeReviewImages(ii::runtime::EventLoop &loop, ii::runtime::Runtime &workers)
    : impl_(std::make_unique<Impl>(loop, workers)) {}
NativeReviewImages::~NativeReviewImages() = default;
std::shared_ptr<const ii::gui::RasterImage>
NativeReviewImages::image(std::string_view source, std::uint32_t width, std::uint32_t height) const {
    const auto address = loaderAddress(source);
    return address ? impl_->loader.image(*address, width, height) : nullptr;
}
void NativeReviewImages::invalidate(std::string_view source) {
    if (const auto address = loaderAddress(source)) impl_->loader.invalidate(*address);
}
void NativeReviewImages::clear() { impl_->loader.clear(); }
} // namespace dreamscapes::review
