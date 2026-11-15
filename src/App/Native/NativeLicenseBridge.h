#pragma once
#include <iiLicenseManager/SocietyLicenseGateModel.h>
#include <functional>
#include <memory>
#include <string>

namespace dreamscapes::native {
// The product owns the authority and supplies snapshots and actions; the SDK
// view model owns presentation only. All callbacks execute on the owner thread.
struct LicenseGateSource final {
    std::function<iisacc::licensing::GateState()> snapshot;
    std::function<bool()> openSociety;
    // Empty when this application accepts a license decision only at launch.
    std::function<void()> retry;
};

class NativeLicenseBridge final {
public:
    NativeLicenseBridge(LicenseGateSource source,std::string openFailure,
        std::shared_ptr<iisacc::licensing::SocietyLicenseGateModel> model = {});
    ~NativeLicenseBridge();
    NativeLicenseBridge(const NativeLicenseBridge&) = delete;
    NativeLicenseBridge& operator=(const NativeLicenseBridge&) = delete;
    [[nodiscard]] std::shared_ptr<iisacc::licensing::SocietyLicenseGateModel> model() const;
    void refresh();
    void setOpenFailure(std::string message);
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
