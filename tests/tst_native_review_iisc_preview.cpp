#include "App/Views/Result/NativeReviewGalleryView.h"
#include "App/Views/Result/NativeReviewImages.h"
#include <File/DocumentFile.h>
#include <Serialization/IiscCodec.h>
#include <iiFileProviderNative.h>
#include <iiGUI/WidgetState.h>
#include <iiNetwork/Wire.h>
#include <iiSDKCore/Runtime/EventLoop.h>
#include <iiSDKCore/Runtime/Runtime.h>
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
template <class T> T take(ii::core::Result<T> value) {
    if (!value) {
        if (value.error().cause) std::rethrow_exception(value.error().cause);
        throw std::runtime_error(value.error().message);
    }
    return std::move(*value);
}
void good(ii::core::Result<void> value) {
    if (!value) {
        if (value.error().cause) std::rethrow_exception(value.error().cause);
        throw std::runtime_error(value.error().message);
    }
}
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
std::string previewSource(const std::filesystem::path &path, std::string_view revision) {
    const auto text = path.u8string();
    auto identifier = ii::network::wire::base64(std::string(text.begin(), text.end()));
    std::ranges::replace(identifier, '+', '-');
    std::ranges::replace(identifier, '/', '_');
    while (!identifier.empty() && identifier.back() == '=') identifier.pop_back();
    return "image://iisc-preview/" + identifier + "?revision=" + std::string(revision);
}
void exercise(const std::filesystem::path &resources) {
    using namespace dreamscapes;
    using namespace std::chrono_literals;
    iiFileProvider::native::TemporaryDirectory temporary(resources);
    check(temporary.isValid(), "Cannot create a workspace test fixture");
    const auto path = temporary.path() / std::filesystem::path(u8"네이티브 파일.iisc");
    iiSharedCanvas::Document document;
    document.extent = {40, 20};
    document.assets.emplace_back(iiSharedCanvas::RasterAsset{"red", makeRasterLayer(40, 20, 0x80ff0000U)});
    document.layers.emplace_back(iiSharedCanvas::StaticBitmapLayer{{"paint", "Paint"}, iiSharedCanvas::StaticSource{"red"}});
    const auto encoded = iiSharedCanvas::encodeIisc(document);
    check(encoded.ok(), "Cannot encode the canvas test fixture");
    const std::string original(reinterpret_cast<const char *>(encoded.bytes.data()), encoded.bytes.size());
    iiFileProvider::native::File::write(path, original);
    const auto source = previewSource(path, "fixture-1");

    ii::runtime::EventLoop loop(64);
    ii::runtime::Runtime workers({.workers = 2, .capacity = 64});
    auto loader = std::make_shared<review::NativeReviewImages>(loop, workers);
    auto state = std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {640, 480}));
    std::shared_ptr<review::ReviewGalleryModel> gallery;
    auto entry = [&](std::string value) {
        return vm::Value(vm::Object{{"id", vm::Value("iisc")},
                                    {"documentSource", vm::Value("file:///document.iisc")},
                                    {"thumbnailSource", vm::Value(std::move(value))},
                                    {"mediaType", vm::Value("Image")}});
    };
    const auto result = entry(source);
    good(root.set_content([&](ii::gui::WidgetBuilder &builder) {
        return review::buildReviewGallery(builder, {}, [&](const auto &model) {
            gallery = model;
            model->setImageResolver(loader);
            model->session()->replaceEntries({result});
        });
    }));
    (void)take(state->snapshots());
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!hasPreview(take(state->drawing_snapshot({640, 480})), false) && std::chrono::steady_clock::now() < deadline) {
        loop.poll();
        std::this_thread::sleep_for(1ms);
    }
    check(hasPreview(take(state->drawing_snapshot({640, 480})), false),
          "A native iisc preview did not reach the LVUI drawing tree with its straight alpha");
    check(workers.stats().submitted > 0, "Canvas preview did not use the worker runtime");
    check(iiFileProvider::native::File::read(path) == original, "Canvas preview modified the original file");
    check(gallery->session()->entryAt(0) == result, "Canvas preview changed the result metadata");

    document.assets.clear();
    document.assets.emplace_back(iiSharedCanvas::RasterAsset{"red", makeRasterLayer(40, 20, 0x8000ff00U)});
    const auto updated = iiSharedCanvas::encodeIisc(document);
    check(updated.ok(), "Cannot update the canvas test fixture");
    const std::string replacement(reinterpret_cast<const char *>(updated.bytes.data()), updated.bytes.size());
    iiFileProvider::native::File::write(path, replacement);
    gallery->session()->replaceEntries({entry(previewSource(path, "fixture-2"))});
    check(!hasPreview(take(state->drawing_snapshot({640, 480})), false), "A changed canvas retained the old preview");
    const auto replacementDeadline = std::chrono::steady_clock::now() + 10s;
    while (!hasPreview(take(state->drawing_snapshot({640, 480})), true) &&
           std::chrono::steady_clock::now() < replacementDeadline) {
        loop.poll();
        std::this_thread::sleep_for(1ms);
    }
    check(hasPreview(take(state->drawing_snapshot({640, 480})), true),
          "A changed canvas revision did not receive a fresh preview");
    check(iiFileProvider::native::File::read(path) == replacement, "A refreshed preview changed the canvas file");

    const auto workingPath = temporary.path() / "working.iisc";
    const auto workingText = workingPath.u8string();
    iiSharedCanvas::DocumentFile working;
    check(working.create(std::string(workingText.begin(), workingText.end()), document).ok(),
          "Cannot create the canvas working-file fixture");
    check(working.edit([](auto &draft) {
        draft.assets.clear();
        draft.assets.emplace_back(iiSharedCanvas::RasterAsset{"red", makeRasterLayer(40, 20, 0x80ff0000U)});
        return true;
    }).ok(), "Cannot set the working-file artwork");
    const auto beforeWalRevision = working.revision();
    working.close();
    sqlite3 *rawDatabase = nullptr;
    const auto dbPath = workingPath.u8string();
    const std::string dbPathUtf8(dbPath.begin(), dbPath.end());
    const int opened = sqlite3_open(dbPathUtf8.c_str(), &rawDatabase);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(rawDatabase, &sqlite3_close);
    check(opened == SQLITE_OK && sqlite3_exec(database.get(),
          "PRAGMA journal_mode=WAL; UPDATE canvas_state SET revision=revision+1 WHERE singleton=1",
          nullptr, nullptr, nullptr) == SQLITE_OK,
          "Cannot enable WAL for the working-file fixture");
    auto wal = workingPath;
    wal += "-wal";
    const auto baseBefore = iiFileProvider::native::File::read(workingPath);
    const bool hadWal = std::filesystem::exists(wal);
    check(hadWal, "The working-file fixture did not create its WAL");
    const auto walBefore = hadWal ? iiFileProvider::native::File::read(wal) : std::string{};
    std::uint64_t latestRevision = 0;
    check(iiSharedCanvas::DocumentFile::readRevision(dbPathUtf8, latestRevision).ok() &&
              latestRevision == beforeWalRevision + 1,
          "The working-file fixture did not expose its committed WAL revision");
    gallery->session()->replaceEntries({entry(previewSource(workingPath, "working"))});
    const auto workingDeadline = std::chrono::steady_clock::now() + 10s;
    while (!hasPreview(take(state->drawing_snapshot({640, 480})), false) &&
           std::chrono::steady_clock::now() < workingDeadline) {
        loop.poll();
        std::this_thread::sleep_for(1ms);
    }
    check(hasPreview(take(state->drawing_snapshot({640, 480})), false), "A canvas with a committed WAL did not render");
    check(iiFileProvider::native::File::read(workingPath) == baseBefore && std::filesystem::exists(wal) == hadWal &&
              (!hadWal || iiFileProvider::native::File::read(wal) == walBefore),
          "Previewing the working file changed its database or WAL");
    database.reset();
    gallery->session()->replaceEntries({entry("image://iisc-preview/invalid!base64")});
    check(!hasPreview(take(state->drawing_snapshot({640, 480})), false), "Invalid preview retained stale pixels");
    good(root.clear_content());
    gallery.reset();
    loader.reset();
    (void)workers.wait_idle();
    loop.poll();
    state->close();
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "A workspace resource directory is required");
        exercise(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
