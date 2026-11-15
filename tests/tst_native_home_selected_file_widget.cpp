#include "App/Views/Home/NativeHomeSelectedFileView.h"
#include "iiGUI/WidgetState.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) { if (!result) throw std::runtime_error(result.error().message); }
template<class T> T take(ii::core::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}
class ImageResolver final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view source, std::uint32_t,
                                                       std::uint32_t) const override {
        return source == "file:///fixture.png" ? pixels : nullptr;
    }
    std::shared_ptr<const ii::gui::RasterImage> pixels;
};
void exercise() {
    auto state = std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); });
    // The Home panel already applies its 12px LVUI spacing around this
    // content; the nested view must use the remaining bounds without adding
    // the same inset a second time.
    auto root = take(state->content_host({}, "root", {491, 469}));
    auto model = home::HomeSelectedFileModel::create();
    auto images = std::make_shared<ImageResolver>();
    images->pixels = take(ii::gui::rgba_image(1, 1, {20, 60, 120, 255}));
    int opened = 0; model->setOpenHandler([&] { ++opened; });
    good(root.set_content([&](ii::gui::WidgetBuilder &builder) {
        return home::buildHomeSelectedFile(builder, {}, model, images);
    }));
    (void)take(state->drawing_snapshot({491, 469}));
    const auto value = [&](const char *id) {
        auto item = root.content_item(id);
        if (!item) {
            for (const auto &snapshot : take(state->snapshots()))
                if (snapshot.id.ends_with(std::string("/") + id)) return snapshot.text;
            throw std::runtime_error(std::string("Missing selected file widget: ") + id);
        }
        return take(item->snapshot()).text;
    };
    check(value("selectedFileHeading") == "Selected file" && value("selectedFileName") == "Select a recent file",
          "empty selected file view was not rendered");
    check(!take(take(root.content_item("openSelectedFile")).snapshot()).enabled,
          "open button is enabled without a selected file");
    const auto file = vm::Value(vm::Object{{"id", vm::Value("canvas")}, {"name", vm::Value("canvas.iisc")},
        {"relativePath", vm::Value("Work/canvas.iisc")}, {"available", vm::Value(false)}});
    const auto generation = take(model->setFile(file, "file:///fixture.png"));
    good(model->setMetadata(generation, vm::Value(vm::Object{{"width", vm::Value(1024)}, {"height", vm::Value(512)},
        {"layerCount", vm::Value(2)}, {"colorSpace", vm::Value("sRGB")}})));
    check(value("selectedFileName") == "canvas.iisc" && value("selectedFilePath") == "Work/canvas.iisc" &&
          value("openSelectedFile") == "Download and open" && value("selectedCanvasValue") == "1024 × 512 px" &&
          value("selectedStructureValue") == "2 layers" && value("selectedColorValue") == "sRGB",
          "native selected file labels did not update");
    const auto content = take(take(root.content_item("selectedFileContent")).snapshot()).bounds;
    const auto preview = take(take(root.content_item("selectedFilePreview")).snapshot()).bounds;
    check(content.width == 491 && content.height == 469 && preview.width == 491 && preview.height > 200,
          "selected file view applied a second inset or did not fill its parent");
    good(take(root.content_item("openSelectedFile")).activate());
    check(opened == 1, "native open action was not delivered");
    const auto frame = take(state->drawing_snapshot({491, 469}));
    auto count = frame.images.size();
    for (const auto &layer : frame.overlays) count += layer.images.size();
    for (const auto &scene : frame.scenes) for (const auto &layer : scene.layers) count += layer.images.size();
    check(count > 0, "selected file image resolver did not render the preview");
    (void)take(state->drawing_snapshot({350, 493}));
    const auto narrow = take(take(root.content_item("selectedFileSurface")).snapshot()).bounds;
    check(narrow.width == 350 && !model->wide.get() && value("selectedCanvasValue") == "1024 × 512 px",
          "selected file metadata did not reflow at a narrow width");
    model->setOpenHandler({});
    check(!take(take(root.content_item("openSelectedFile")).snapshot()).enabled,
          "retired open action remained enabled");
    state->close();
}
} // namespace
int main() {
    try { exercise(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
