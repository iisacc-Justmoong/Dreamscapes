#include "App/Views/Home/NativeHomeQueueBridge.h"
#include "App/Views/Home/NativeHomeQueueView.h"
#include "App/ViewModels/GenerationProjection.h"
#include "iiGUI/WidgetState.h"
#include <QCoreApplication>
#include <QVariantMap>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) { if (!result) throw std::runtime_error(result.error().message); }
template<class T> T take(ii::core::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void exercise(const char *path) {
    using namespace dreamscapes;
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Figma indicator missing");
    std::string svg{std::istreambuf_iterator<char>(input), {}};
    auto state = std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {526, 160}));
    auto model = std::make_shared<home::HomeQueueModel>();
    good(root.set_content([&](ii::gui::WidgetBuilder &builder) {
        return home::buildHomeQueue(builder, {}, model, svg);
    }));
    auto projection = std::make_unique<GenerationProjection>();
    int opens = 0;
    home::NativeHomeQueueBridge bridge(projection.get(), model, [&] { ++opens; });
    bridge.setRuntimeNames("LTX 2.3", "Metal");
    projection->setJobs({QVariantMap{{"id", "a"}, {"state", "running"}},
                         QVariantMap{{"id", "b"}, {"state", "queued"}}});
    auto text = [&](const char *id) { return take(take(root.content_item(id)).snapshot()).text; };
    check(text("queueSummary") == "1 running · 1 waiting · 0 failed", "live jobs did not reach the LVUI view");
    check(text("queueModel") == "LTX 2.3" && text("queueDevice") == "Metal", "runtime names did not reach the LVUI view");
    good(take(root.content_item("openQueue")).activate());
    check(opens == 1, "native button did not invoke the application action");
    projection.reset();
    check(!take(take(root.content_item("openQueue")).snapshot()).enabled, "retired action stayed enabled");
    state->close();
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try { if (argc != 2) return 2; exercise(argv[1]); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Generation projection reaches the LVUI Home queue and its action\n";
}
