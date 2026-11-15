#include "NativeLicenseGateView.h"
#include "NativeSocietyLicenseGate.generated.h"
#include <iiGUI/Desktop.h>
#include <exception>

namespace dreamscapes::native {
ii::core::Result<std::shared_ptr<NativeLicenseBridge>> buildLicenseGate(
    ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<LaunchLicense> license) {
    using View=lvui_generated::NativeSocietyLicenseGate;
    using Model=iisacc::licensing::SocietyLicenseGateModel;
    if (!license) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"License gate requires a launch session");
    try {
        std::shared_ptr<View::Scope> scope;
        if (auto built=View::build(builder,parent,[](auto &) {},&scope); !built) return std::unexpected(built.error());
        if (!scope) return ii::core::fail(ii::core::ErrorCode::failed,"License gate scope is unavailable");
        auto model=std::shared_ptr<Model>(scope,&scope->backend_0);
        iisacc::licensing::GateStrings strings;
        strings.retry="Restart Dreamscapes to check again";
        strings.fallback="Open Society and sign in, then restart Dreamscapes.";
        model->setStrings(std::move(strings));
        const std::weak_ptr<LaunchLicense> weak=license;
        LicenseGateSource source{
            [weak] {
                iisacc::licensing::GateState result;result.productName="Dreamscapes";
                if (auto value=weak.lock()) {result.checking=value->checking.get();result.statusCode=value->code.get();}
                return result;
            },
            [] {return bool(ii::gui::open_external_url("society://"));},
            {} // A new decision requires an application restart.
        };
        return std::make_shared<NativeLicenseBridge>(std::move(source),
            "Could not open Society. Open it on this device and restart Dreamscapes.",std::move(model));
    } catch (...) {
        return ii::core::fail(ii::core::ErrorCode::failed,"Cannot create the Society license gate",std::current_exception());
    }
}
}
