#include "NativeApplicationShellView.h"
#include "NativeApplicationShell.generated.h"
#include "NativeLicenseGateView.h"
#include "NativeTitleBarView.h"
#include "App/Views/Home/NativeHomeWorkspaceView.h"

namespace dreamscapes::native {
ii::core::Result<void> buildNativeApplicationShell(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,
        std::shared_ptr<NativeApplicationShell> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Native shell requires a model");
    auto root=builder.zstack(parent,"applicationRoot",{.alignment=ii::gui::StackAlignment::stretch});
    if (!root) return std::unexpected(root.error());
    if (auto flex=root->set_flex(1); !flex) return flex;
    auto layout=builder.vstack(*root,"applicationLayout",{.spacing=0,.alignment=ii::gui::StackAlignment::stretch});
    if (!layout) return std::unexpected(layout.error());
    if (auto title=buildNativeTitleBar(builder,*layout,model->titleBar()); !title) return title;
    auto host=builder.content_host(*layout,"applicationContent");
    if (!host) return std::unexpected(host.error());
    if (auto flex=host->set_flex(1); !flex) return flex;
    auto gateHost=builder.content_host(*root,"applicationLicenseGate");
    if (!gateHost) return std::unexpected(gateHost.error());
    model->setRoot(*root);
    const std::weak_ptr<NativeApplicationShell> weak=model;
    auto mount=[host=*host,weak]() -> ii::core::Result<void> {
        auto value=weak.lock();
        if (!value) return ii::core::fail(ii::core::ErrorCode::closed,"Native shell has closed");
        return host.set_content([value](ii::gui::WidgetBuilder &content) {
            if (value->atHome.get()) return home::buildHomeWorkspace(content,{},value->workspace());
            auto factories=lvui::native::BackendFactories{}.with<NativeApplicationShellBackend>([value] {return NativeApplicationShellBackend(value);});
            return lvui_generated::NativeApplicationShell::build(content,{},[](auto &) {},nullptr,factories);
        });
    };
    // Give the popup its own lifetime so a license decision cannot replace the workspace generation.
    auto mountGate=[host=*gateHost,weak]() -> ii::core::Result<void> {
        auto value=weak.lock();
        if (!value) return ii::core::fail(ii::core::ErrorCode::closed,"Native shell has closed");
        const auto license=value->launchLicense();
        if (!license || !license->gateVisible.get()) {
            if (auto cleared=host.clear_content(); !cleared) return cleared;
            value->setLicenseGateBridge({});
            return {};
        }
        std::shared_ptr<NativeLicenseBridge> bridge;
        auto mounted=host.set_content([&](ii::gui::WidgetBuilder &content) -> ii::core::Result<void> {
            auto gate=buildLicenseGate(content,{},license);
            if (!gate) return std::unexpected(gate.error());
            bridge=std::move(*gate);
            return {};
        });
        if (mounted) value->setLicenseGateBridge(std::move(bridge));
        return mounted;
    };
    model->setViewChangeHandler(mount);
    model->setLicenseGateChangeHandler(mountGate);
    if (auto mounted=mount(); !mounted) return mounted;
    return mountGate();
}
}
