#include "App/Native/NativeLicenseBridge.h"
#include <memory>
#include <stdexcept>

using namespace dreamscapes::native;
using iisacc::licensing::GateState;
static void check(bool value,const char* error){if(!value)throw std::runtime_error(error);}

int main(){
    GateState state;state.productName="Dreamscapes";state.statusCode="license_not_owned";
    int reads=0,opens=0,retries=0;bool openResult=false;
    auto bridge=std::make_unique<NativeLicenseBridge>(LicenseGateSource{
        [&]{++reads;return state;},[&]{++opens;return openResult;},[&]{++retries;}
    },"Could not open Society");
    auto model=bridge->model();
    check(reads==1&&model->status.get().find("Dreamscapes")!=std::string::npos,"Initial license snapshot was lost");
    model->requestOpenSociety();
    check(opens==1&&model->status.get()=="Could not open Society","Open failure was not shown");
    openResult=true;model->requestOpenSociety();
    check(opens==2&&model->status.get().find("Dreamscapes")!=std::string::npos,"Successful open did not clear the error");
    state.checking=true;bridge->refresh();model->requestRetry();
    check(retries==0&&!model->retryEnabled.get(),"A license check was restarted while checking");
    state.checking=false;bridge->refresh();model->requestRetry();
    check(retries==1&&model->retryEnabled.get(),"Explicit retry was not forwarded");
    const auto count=reads;bridge.reset();model->requestOpenSociety();model->requestRetry();
    check(reads==count&&opens==2&&retries==1,"A retired bridge handled an action");

    std::unique_ptr<NativeLicenseBridge> reentrant;
    auto onOpen=[&]{reentrant.reset();return false;};
    reentrant=std::make_unique<NativeLicenseBridge>(LicenseGateSource{[&]{return state;},onOpen,[]{}},"Open failed");
    auto retained=reentrant->model();retained->requestOpenSociety();
    check(!reentrant&&retained->status.get()=="Open failed","Reentrant bridge destruction lost the action result");
    auto external=std::make_shared<iisacc::licensing::SocietyLicenseGateModel>();
    NativeLicenseBridge attached(LicenseGateSource{[&]{return state;},[]{return true;},[]{}},"",external);
    check(attached.model()==external&&external->status.get().find("Dreamscapes")!=std::string::npos,
          "The generated LVUI scope's model was not bound");
    state.checking=true;
    NativeLicenseBridge checking(LicenseGateSource{[&]{return state;},[]{return false;},[]{}},"Open failed");
    auto checkingModel=checking.model();checkingModel->requestOpenSociety();
    check(checkingModel->status.get()=="Open failed","An open failure while checking was hidden");
    state.checking=false;
    NativeLicenseBridge launchOnly(LicenseGateSource{[&]{return state;},[]{return true;},{}},"Open failed");
    auto launchOnlyModel=launchOnly.model();
    check(!launchOnlyModel->retryEnabled.get(),"A launch-only license enabled retry");
    launchOnlyModel->requestRetry();
    check(!launchOnlyModel->retryEnabled.get(),"A launch-only license restarted its check");
}
