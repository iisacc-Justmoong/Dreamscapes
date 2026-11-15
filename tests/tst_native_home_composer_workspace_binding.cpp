#include "App/Views/Home/NativeHomeComposerWorkspaceBinding.h"
#include <iiGUI/CompiledViewSource.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
bool near(double a, double b) { return std::abs(a-b) < .01; }
void exercise() {
    auto fixture = ii::gui::compiled_view_source([](ii::gui::WidgetBuilder &) -> ii::core::Result<void> { return {}; });
    auto composer = HomeComposerModel::create(); auto prompt = HomeComposerPromptModel::create(composer,HomeComposerTextRole::prompt,"","Prompt","Clear",true);
    HomeComposerPanelSources parts; parts.top.fill(fixture); parts.settings.fill(fixture); parts.attachments = fixture;
    const HomeComposerLayoutSpec spec{1000,600,400,{88,1,22,93,138,145},{327,88,144,96,120,228,152}};
    const HomeComposerLayoutMetrics metrics{8,12,22,37,44};
    auto panel = HomeComposerPanelModel::create(composer,prompt,parts,spec,metrics); good(panel->setViewport(1227,400));
    auto sidebar = HomeSidebarModel::create({{"home","Home","home"}});
    HomeWorkspaceSources sources{fixture,fixture,fixture,fixture,fixture,fixture};
    auto workspace = HomeWorkspaceModel::create(sources,sidebar,{"Home","","Open","New","Storage"},{});
    auto binding = HomeComposerWorkspaceBinding::create(panel,workspace);
    check(near(workspace->composerHeight.get(),panel->requestedHeight.get()), "initial composer height was not published");
    good(prompt->setMeasuredHeight(70,2,22));
    check(near(workspace->composerHeight.get(),panel->requestedHeight.get()), "prompt growth did not resize the workspace");
    auto nested = workspace->windowWidth.observe([&](double, std::uint64_t) { good(panel->setViewport(687,400)); });
    good(workspace->setViewport(900,640,12,16));
    check(near(workspace->composerHeight.get(),panel->requestedHeight.get()), "composer resize during workspace publication was lost");
    nested.reset(); const auto prior = workspace->composerHeight.get(); binding->close();
    good(panel->setViewport(360,400)); check(near(workspace->composerHeight.get(),prior), "closed binding still changed the workspace");
    check(panel->active() && workspace->active() && composer->active(), "binding retired shared models");
    panel->close(); workspace->close(); prompt->close(); composer->close(); sidebar->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
