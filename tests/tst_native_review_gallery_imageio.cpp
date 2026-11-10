#include "App/Views/Result/NativeReviewGalleryView.h"
#include "App/Views/Result/NativeReviewImages.h"
#include <iiGUI/WidgetState.h>
#include <iiSDKCore/Runtime/EventLoop.h>
#include <iiSDKCore/Runtime/Runtime.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class T> T take(ii::core::Result<T> value) {
    if (!value) {
        if (value.error().cause)
            std::rethrow_exception(value.error().cause);
        throw std::runtime_error(value.error().message);
    }
    return std::move(*value);
}
void good(ii::core::Result<void> value) {
    if (!value) {
        if (value.error().cause)
            std::rethrow_exception(value.error().cause);
        throw std::runtime_error(value.error().message);
    }
}
std::size_t matchingImages(const ii::gui::CanvasFrame &frame) {
    std::size_t count = 0;
    const auto inspect = [&](const auto &images) {
        for (const auto &image : images)
            if (image.image && image.image->width() == 48 && image.image->height() == 48)
                ++count;
    };
    inspect(frame.images);
    for (const auto &layer : frame.overlays)
        inspect(layer.images);
    for (const auto &scene : frame.scenes)
        for (const auto &layer : scene.layers)
            inspect(layer.images);
    return count;
}
void exercise(const std::string &path) {
    using namespace dreamscapes;
    using namespace std::chrono_literals;
    ii::runtime::EventLoop loop(64);
    ii::runtime::Runtime workers({.workers = 2, .capacity = 64});
    auto loader = std::make_shared<review::NativeReviewImages>(loop, workers);
    auto state = std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {640, 480}));
    std::shared_ptr<review::ReviewGalleryModel> gallery;
    auto entry = [&](std::string preview) {
        return vm::Value(vm::Object{{"id", vm::Value("real-image")},
                                    {"documentSource", vm::Value("file:///result.iisc")},
                                    {"thumbnailSource", vm::Value(std::move(preview))},
                                    {"mediaType", vm::Value("Image")}});
    };
    const auto original = entry(path);
    good(root.set_content([&](ii::gui::WidgetBuilder &builder) {
        return review::buildReviewGallery(builder, {}, [&](const auto &model) {
            gallery = model;
            model->setImageResolver(loader);
            model->session()->replaceEntries({original});
        });
    }));
    (void)take(state->snapshots());
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (matchingImages(take(state->drawing_snapshot({640, 480}))) == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        loop.poll();
        std::this_thread::sleep_for(1ms);
    }
    if (matchingImages(take(state->drawing_snapshot({640, 480}))) != 1) {
        throw std::runtime_error(std::string("The actual LVUI ImageIO loader did not decode and display the PNG; submitted=") +
                                 std::to_string(workers.stats().submitted) +
                                 " results=" + std::to_string(gallery->items.get().size()));
    }
    check(gallery->session()->entryAt(0) == original, "Preview loading changed the result metadata");
    gallery->session()->replaceEntries({entry(path + ".missing")});
    check(matchingImages(take(state->drawing_snapshot({640, 480}))) == 0,
          "A changed preview source retained the previous image");
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
        check(argc == 2, "A workspace PNG fixture is required");
        exercise(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
