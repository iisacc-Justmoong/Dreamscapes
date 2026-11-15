#include "App/Views/Home/NativeHomeComposerFactory.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
HomeComposerLabels labels() {
    return {"Describe", "Prompt", "Clear", "Negative prompt", "Done", "Media", "Model", "Choose model",
            "Aspect ratio", "Resolution", "Batch size", "Duration", "Frame rate", "Preset", "Load preset",
            "Save preset", "Name", "Save", "Cancel", "Seed", "Random", "Apply", "Attach images", "Paint canvas",
            "Advanced parameters", "More controls", "Generate", [](int n, bool video) { return std::to_string(n)+(video?" videos":" images"); },
            [](const auto &name) { return "Remove " + name; }};
}
void exercise() {
    auto composer = HomeComposerModel::create();
    const HomeComposerLayoutSpec spec{1000,600,400,{88,1,22,93,138,145},{327,88,144,96,120,228,152}};
    const HomeComposerLayoutMetrics metrics{8,12,22,37,44};
    auto panel = createHomeComposerPanel(composer,labels(),spec,metrics);
    check(panel && panel->active() && homeComposerPanelSource(panel), "factory did not create a complete composer source");
    bool rejected = false; auto missing = labels(); missing.removeAttachment = {};
    try { (void)createHomeComposerPanel(composer,std::move(missing),spec,metrics); } catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "missing accessible removal formatter was accepted");
    rejected = false; try { (void)homeComposerPanelSource({}); } catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "missing panel source was accepted");
    panel->close(); check(composer->active(), "closing the constructed panel closed the shared generation draft");
    composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
