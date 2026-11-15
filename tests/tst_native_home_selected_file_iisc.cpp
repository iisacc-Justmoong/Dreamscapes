#include "App/Native/NativeFileImages.h"
#include "App/Views/Home/NativeHomeSelectedFileView.h"
#include <Serialization/IiscCodec.h>
#include <iiFileProviderNative.h>
#include <iiGUI/WidgetState.h>
#include <iiNetwork/Wire.h>
#include <iiSDKCore/Runtime/EventLoop.h>
#include <iiSDKCore/Runtime/Runtime.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace dreamscapes;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
template<class T> T take(ii::core::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void good(ii::core::Result<void> result) { if (!result) throw std::runtime_error(result.error().message); }
bool hasPreview(const ii::gui::CanvasFrame &frame, bool green) {
    const auto match = [green](const auto &images) {
        return std::ranges::any_of(images, [green](const auto &image) {
            if (!image.image || image.image->width() != 40 || image.image->height() != 20) return false;
            const auto rgba = image.image->rgba();
            return rgba.size() >= 4 && rgba[0] == (green ? 0 : 255) && rgba[1] == (green ? 255 : 0) &&
                   rgba[2] == 0 && rgba[3] == 128;
        });
    };
    if (match(frame.images)) return true;
    for (const auto &layer : frame.overlays) if (match(layer.images)) return true;
    for (const auto &scene : frame.scenes) for (const auto &layer : scene.layers) if (match(layer.images)) return true;
    return false;
}
std::string address(const std::filesystem::path &path, std::string_view revision) {
    const auto bytes = path.u8string();
    auto encoded = ii::network::wire::base64(std::string(bytes.begin(), bytes.end()));
    std::ranges::replace(encoded, '+', '-'); std::ranges::replace(encoded, '/', '_');
    while (!encoded.empty() && encoded.back() == '=') encoded.pop_back();
    return "image://iisc-preview/" + encoded + "?revision=" + std::string(revision);
}
void exercise(const std::filesystem::path &resources) {
    using namespace std::chrono_literals;
    iiFileProvider::native::TemporaryDirectory temporary(resources);
    check(temporary.isValid(), "cannot create workspace fixture");
    const auto path = temporary.path() / std::filesystem::path(u8"선택 파일.iisc");
    iiSharedCanvas::Document document; document.extent = {40, 20};
    document.assets.emplace_back(iiSharedCanvas::RasterAsset{"red", makeRasterLayer(40, 20, 0x80ff0000U)});
    document.layers.emplace_back(iiSharedCanvas::StaticBitmapLayer{{"paint", "Paint"}, iiSharedCanvas::StaticSource{"red"}});
    const auto encoded = iiSharedCanvas::encodeIisc(document); check(encoded.ok(), "cannot encode fixture");
    const std::string bytes(reinterpret_cast<const char *>(encoded.bytes.data()), encoded.bytes.size());
    iiFileProvider::native::File::write(path, bytes);
    ii::runtime::EventLoop loop(64);
    ii::runtime::Runtime workers({.workers = 2, .capacity = 64});
    auto images = std::make_shared<native::NativeFileImages>(loop, workers);
    auto state = std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {515, 493}));
    auto model = home::HomeSelectedFileModel::create();
    good(root.set_content([&](ii::gui::WidgetBuilder &builder) {
        return home::buildHomeSelectedFile(builder, {}, model, images);
    }));
    const auto original = vm::Value(vm::Object{{"id", vm::Value("first")}, {"name", vm::Value("선택 파일.iisc")}});
    (void)take(model->setFile(original, address(path, "one")));
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!hasPreview(take(state->drawing_snapshot({515, 493})), false) && std::chrono::steady_clock::now() < deadline) {
        loop.poll(); std::this_thread::sleep_for(1ms);
    }
    check(hasPreview(take(state->drawing_snapshot({515, 493})), false), "actual iisc preview was not rendered");
    check(workers.stats().submitted > 0 && iiFileProvider::native::File::read(path) == bytes && model->file() == original,
          "preview read did not use workers or changed the source file or model");
    document.assets.clear();
    document.assets.emplace_back(iiSharedCanvas::RasterAsset{"red", makeRasterLayer(40, 20, 0x8000ff00U)});
    const auto changed = iiSharedCanvas::encodeIisc(document); check(changed.ok(), "cannot update fixture");
    const std::string replacement(reinterpret_cast<const char *>(changed.bytes.data()), changed.bytes.size());
    iiFileProvider::native::File::write(path, replacement);
    (void)take(model->setFile(original, address(path, "two")));
    check(!hasPreview(take(state->drawing_snapshot({515, 493})), false), "old preview remained after revision change");
    const auto nextDeadline = std::chrono::steady_clock::now() + 10s;
    while (!hasPreview(take(state->drawing_snapshot({515, 493})), true) && std::chrono::steady_clock::now() < nextDeadline) {
        loop.poll(); std::this_thread::sleep_for(1ms);
    }
    check(hasPreview(take(state->drawing_snapshot({515, 493})), true) &&
          iiFileProvider::native::File::read(path) == replacement, "updated iisc preview was not refreshed safely");
    good(root.clear_content()); model.reset(); images.reset(); (void)workers.wait_idle(); loop.poll(); state->close();
}
}
int main(int argc, char **argv) {
    try { if (argc != 2) return 2; exercise(argv[1]); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
