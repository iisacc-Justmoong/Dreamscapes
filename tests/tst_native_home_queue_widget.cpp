#include "App/Views/Home/NativeHomeQueueView.h"
#include "iiGUI/WidgetState.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace dreamscapes;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) { if (!result) throw std::runtime_error(result.error().message); }
template<class T> T take(ii::core::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void exercise(const char *path) {
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Figma ready indicator missing");
    std::string svg{std::istreambuf_iterator<char>(input), {}};
    auto state = std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {526, 160}));
    auto model = std::make_shared<home::HomeQueueModel>();
    int actions = 0; model->setOpenQueueHandler([&] { ++actions; });
    good(root.set_content([&](ii::gui::WidgetBuilder &builder) {
        return home::buildHomeQueue(builder, {}, model, svg);
    }));
    auto value = [&](const char *id) { return take(take(root.content_item(id)).snapshot()).text; };
    check(value("queueStatus") == "Ready to generate" &&
          value("queueSummary") == "0 running · 0 waiting · 0 failed", "initial labels missing");
    check(value("queueModel") == "Unavailable" && value("queueDevice") == "Automatic", "runtime labels missing");
    const auto indicator = take(take(root.content_item("readyIndicator")).snapshot());
    check(indicator.bounds.width == 6 && indicator.bounds.height == 6, "indicator dimensions changed");
    const auto frame = take(state->drawing_snapshot({526, 160}));
    auto images = frame.images.size();
    for (const auto &layer : frame.overlays) images += layer.images.size();
    for (const auto &scene : frame.scenes) for (const auto &layer : scene.layers) images += layer.images.size();
    check(images > 0, "Figma indicator was not rendered");
    good(take(root.content_item("openQueue")).activate());
    check(actions == 1, "queue action missing");
    home::HomeQueueSnapshot snapshot;
    snapshot.jobs = vm::Value(vm::Array{vm::Value(vm::Object{{"state", vm::Value("running")}})});
    snapshot.modelName = "LTX 2.3"; snapshot.deviceName = "Metal";
    good(model->setSnapshot(snapshot));
    check(value("queueStatus") == "Generating…" && value("queueSummary") == "1 running · 0 waiting · 0 failed" &&
          value("queueModel") == "LTX 2.3" && value("queueExecution") == "Running", "reactive labels did not update");
    model->setOpenQueueHandler({});
    check(!take(take(root.content_item("openQueue")).snapshot()).enabled, "detached action remains enabled");
    state->close();
}
}
int main(int argc, char **argv) {
    try { if (argc != 2) return 2; exercise(argv[1]); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Native Home queue renders and reacts without a window\n";
}
