#include "App/Views/Home/NativeHomeSidebar.h"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace dreamscapes;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) { if (!result) { if (result.error().cause) std::rethrow_exception(result.error().cause); throw std::runtime_error(result.error().message); } }
void exercise() {
    auto model = home::HomeSidebarModel::create({{"home", "Home", "nodeshomeFolder"}, {"divider1", "", "", true},
        {"image", "Image", "unconditionalImageGeneration"}, {"history", "Generation History", "profileCPU"}});
    std::string activated; int calls = 0;
    model->setHandler([&](std::string key) { activated = std::move(key); ++calls; });
    good(model->setSelected("home"));
    auto rows = model->rows.get();
    check(rows.size() == 4 && rows[0].selected && !rows[1].selected && rows[2].label == "Image", "initial navigation projection is wrong");
    good(model->activate("image")); check(activated == "image" && calls == 1 && model->selected.get() == "home", "action mutated selection or was not delivered");
    check(!model->activate("divider1") && !model->activate("missing") && calls == 1, "divider or unknown action was delivered");
    good(model->setSelected("history")); check(model->rows.get()[3].selected && !model->rows.get()[0].selected, "external selection was not reflected");
    good(model->setCompact(true)); rows = model->rows.get();
    check(rows[2].label.empty() && rows[2].accessible == "Image" && rows[2].icon == "unconditionalImageGeneration" && model->compact.get(),
        "compact mode lost icon or accessibility text");
    good(model->setCompact(false)); check(model->rows.get()[2].label == "Image", "expanded labels did not return");
    bool rejected = false;
    std::thread outsider([&] { rejected = !model->activate("home"); }); outsider.join();
    check(rejected && calls == 1, "wrong-thread input reached the controller");
    model->close(); check(!model->activate("home"), "closed sidebar still accepted input");
    bool duplicateRejected = false;
    try { (void)home::HomeSidebarModel::create({{"same", "One", ""}, {"same", "Two", ""}}); }
    catch (const std::invalid_argument &) { duplicateRejected = true; }
    check(duplicateRejected, "duplicate sidebar action identifiers were accepted");
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
