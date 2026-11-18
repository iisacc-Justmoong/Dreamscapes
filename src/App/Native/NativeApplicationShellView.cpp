#include "NativeApplicationShellView.h"
#include "NativeApplicationShell.generated.h"
#include "NativeLicenseGateView.h"
#include "NativeTitleBarView.h"
#include "App/Views/Home/NativeHomeWorkspaceView.h"

namespace dreamscapes::native {
ii::core::Result<void> buildNativeApplicationShell(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,
        std::shared_ptr<NativeApplicationShell> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Native shell requires a model");
    auto root=builder.vstack(parent,"applicationRoot",{.spacing=0,.alignment=ii::gui::StackAlignment::stretch});
    if (!root) return std::unexpected(root.error());
    if (auto flex=root->set_flex(1); !flex) return flex;
    if (auto title=buildNativeTitleBar(builder,*root,model->titleBar()); !title) return title;
    auto host=builder.content_host(*root,"applicationContent");
    if (!host) return std::unexpected(host.error());
    if (auto flex=host->set_flex(1); !flex) return flex;
    model->setRoot(*root);
    const std::weak_ptr<NativeApplicationShell> weak=model;
    auto mount=[host=*host,weak]() -> ii::core::Result<void> {
        auto value=weak.lock();
        if (!value) return ii::core::fail(ii::core::ErrorCode::closed,"Native shell has closed");
        return host.set_content([value](ii::gui::WidgetBuilder &content) {
            if (auto license=value->launchLicense();license && license->gateVisible.get()) {
                auto gate=buildLicenseGate(content,{},std::move(license));
                if (!gate) return ii::core::Result<void>{std::unexpected(gate.error())};
                value->setLicenseGateBridge(std::move(*gate));
                return ii::core::Result<void>{};
            }
            value->setLicenseGateBridge({});
            if (value->atHome.get()) return home::buildHomeWorkspace(content,{},value->workspace());
            auto factories=lvui::native::BackendFactories{}.with<NativeApplicationShellBackend>([value] {return NativeApplicationShellBackend(value);});
            return lvui_generated::NativeApplicationShell::build(content,{},[](auto &) {},nullptr,factories);
        });
    };
    model->setViewChangeHandler(mount);
    return mount();
}
}
