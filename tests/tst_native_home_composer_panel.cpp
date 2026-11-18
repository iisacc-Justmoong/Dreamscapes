#include "App/Views/Home/NativeHomeComposerPanel.h"
#include <iiGUI>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
void exercise() {
    auto composer = HomeComposerModel::create(); int commands = 0;
    good(composer->setHandler([&](const HomeComposerCommand &) -> ii::core::Result<void> { ++commands; return {}; }));
    auto prompt = HomeComposerPromptModel::create(composer, HomeComposerTextRole::prompt, "", "Prompt", "Clear", true);
    auto source = ii::gui::compiled_view_source([](ii::gui::WidgetBuilder &) -> ii::core::Result<void> { return {}; });
    HomeComposerPanelSources sources; sources.top.fill(source); sources.settings.fill(source); sources.attachments = source;
    const HomeComposerLayoutSpec spec{1000,600,400,{88,1,22,93,138,145},{327,88,144,96,120,228,152}};
    const HomeComposerLayoutMetrics metrics{8,12,22,37,44};
    auto model = HomeComposerPanelModel::create(composer, prompt, sources, spec, metrics);
    good(model->setViewport(1227, 300));
    check(model->contentWidth.get() == 1203 && model->topHeight.get() == 22 && model->requestedHeight.get() == 91 && commands == 0,
          "composer panel failed to project the wide layout");
    const auto original = model->layout.get(); auto invalid = metrics; invalid.settingHeight = 1;
    check(!model->setViewport(1227,300,invalid) && model->layout.get() == original, "invalid LVUI metrics replaced the prior layout");
    good(prompt->setMeasuredHeight(50,2,22)); check(model->topHeight.get() == 54 && commands == 0,
                                                  "prompt measurement did not resize the panel");
    auto snapshot = composer->snapshot.get(); snapshot.video = true; snapshot.attachments = {{"a","one.png","",""}};
    snapshot.errorText = "Error"; good(composer->setSnapshot(snapshot)); good(model->setErrorHeight(30));
    check(model->hasAttachments.get() && model->hasError.get() && model->layout.get().settings[8].visible &&
          model->requestedHeight.get() > 91 && commands == 0, "video, attachment or error projection was lost");
    good(model->setViewport(360, 300)); check(model->layout.get().settings[5].width == model->contentWidth.get(),
                                             "narrow preset did not occupy its original span");
    auto bad = sources; bad.settings[3].reset(); bool rejected = false;
    try { (void)HomeComposerPanelModel::create(composer,prompt,bad,spec,metrics); } catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "missing composer source was silently accepted");
    model->close(); check(!model->setViewport(800,300) && composer->active() && prompt->active(), "closing panel changed its shared draft");
    prompt->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
