#include "App/Views/Home/NativeHomePresetGroup.h"
#include <iiGUI/CompiledViewSource.h>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
void exercise() {
    auto source = ii::gui::compiled_view_source([](ii::gui::WidgetBuilder &) -> ii::core::Result<void> { return {}; });
    auto model = HomePresetGroupModel::create("Preset", source, source);
    good(model->setViewport(228,37,4,52));
    check(model->viewportWidth.get() == 228 && model->choiceWidth.get() == 172, "preset sizing did not match Figma");
    check(!model->setViewport(std::numeric_limits<double>::infinity(),37,4,52) && model->choiceWidth.get() == 172,
          "invalid geometry changed the preset row");
    bool rejected = false; try { (void)HomePresetGroupModel::create("Preset", source, {}); }
    catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "missing save control was silently accepted");
    model->close(); check(!model->setViewport(200,37,4,52), "closed preset row accepted layout");
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
