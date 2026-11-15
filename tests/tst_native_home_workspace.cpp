#include "App/Views/Home/NativeHomeWorkspace.h"
#include <iiGUI/NativeView.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
bool near(double a, double b) { return std::abs(a-b) < 0.01; }
class TestSource final : public ii::gui::NativeViewSource {
    ii::core::Result<void> build(ii::gui::WidgetBuilder &) const override { return {}; }
};
void exercise() {
    auto source = std::make_shared<TestSource>();
    HomeWorkspaceSources sources{source, source, source, source, source, source};
    auto sidebar = HomeSidebarModel::create({{"home", "Home", "home"}});
    int opens = 0, canvases = 0;
    auto model = HomeWorkspaceModel::create(sources, sidebar,
        {"Workspace", "Create and continue", "Open file", "New canvas", "Storage"},
        {[&] { ++opens; }, [&] { ++canvases; }});
    const auto viewport = [&](double width, double height) { return model->setViewport(width, height, 12, 16); };
    good(model->setComposerHeight(91));
    good(viewport(1440, 868));
    check(!model->compact.get() && model->wide.get() && near(model->sidebarWidth.get(),181)
        && near(model->contentWidth.get(),1227) && near(model->fileWidth.get(),700)
        && near(model->selectedWidth.get(),515) && near(model->queueWidth.get(),526)
        && near(model->generationWidth.get(),689) && near(model->fileHeight.get(),493), "wide layout is wrong");
    good(model->setStatus("Running"));
    good(viewport(900, 640));
    check(!model->wide.get() && !model->compact.get() && near(model->contentWidth.get(),687)
        && near(model->fileWidth.get(),687) && near(model->selectedWidth.get(),687)
        && near(model->fileHeight.get(),360) && model->status.get()=="Running", "stacked layout is wrong");
    good(viewport(600,600));
    check(model->compact.get() && sidebar->compact.get() && near(model->sidebarWidth.get(),48)
        && near(model->contentWidth.get(),528), "compact layout is wrong");
    good(viewport(700,600));
    check(!model->compact.get() && near(model->sidebarWidth.get(),181), "sidebar breakpoint is wrong");
    good(viewport(1220,600)); check(!model->wide.get(), "workspace entered wide layout too early");
    good(viewport(1221,600)); check(model->wide.get(), "workspace failed to enter the wide layout");
    good(viewport(600,600));
    check(!viewport(std::numeric_limits<double>::quiet_NaN(),600)
        && !model->setComposerHeight(-1) && near(model->contentWidth.get(),528), "invalid dimensions changed the layout");
    check(!model->setViewport(600,600,-1,16) && near(model->contentWidth.get(),528), "invalid spacing changed the layout");
    good(model->setViewport(900,640,8,20));
    check(near(model->inset.get(),20) && near(model->contentWidth.get(),679) &&
          near(model->filePairHeight.get(),728) && near(model->geometry.get().gap,8),
          "workspace did not use the supplied LVUI metrics");
    auto heightDuringResize = model->windowWidth.observe([&](double, std::uint64_t) { good(model->setComposerHeight(140)); });
    good(model->setViewport(1440,868,12,16));
    check(near(model->composerHeight.get(),140) && near(model->fileHeight.get(),444),
          "composer height reported during resize was lost");
    heightDuringResize.reset();
    check(opens==0 && canvases==0, "layout update invoked an application command");
    good(model->openFile()); good(model->newCanvas());
    check(opens==1 && canvases==1, "header actions were not delivered");
    bool wrongThread = false;
    std::thread worker([&] { wrongThread = !model->openFile(); }); worker.join();
    check(wrongThread && opens==1, "command was accepted from the wrong thread");
    model->close(); check(!model->openFile() && opens==1 && !model->canOpenFile.get() && !model->canNewCanvas.get(),
                          "closed workspace delivered or retained an enabled action");
    sources.composer.reset(); bool missingRejected = false;
    try { (void)HomeWorkspaceModel::create(sources, sidebar, {}, {}); }
    catch (const std::invalid_argument &) { missingRejected = true; }
    check(missingRejected, "missing generation composer was silently accepted");
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
