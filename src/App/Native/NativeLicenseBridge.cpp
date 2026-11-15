#include "NativeLicenseBridge.h"
#include <stdexcept>
#include <utility>

namespace dreamscapes::native {
struct NativeLicenseBridge::State final {
    LicenseGateSource source;
    std::string openFailure,actionError;
    bool checking=false;
    std::shared_ptr<iisacc::licensing::SocietyLicenseGateModel> model;

    void refresh(){
        auto current=source.snapshot();
        if(current.checking&&!checking)actionError.clear();
        checking=current.checking;
        current.actionError=actionError;
        (void)model->update.send({std::move(current)});
    }
    void open(){
        const bool ok=source.openSociety();
        actionError=ok?std::string{}:openFailure;
        refresh();
    }
    void retry(){
        actionError.clear();
        refresh();
        source.retry();
    }
};

NativeLicenseBridge::NativeLicenseBridge(LicenseGateSource source,std::string openFailure,
                                         std::shared_ptr<iisacc::licensing::SocietyLicenseGateModel> model)
    :state_(std::make_shared<State>()){
    if(!source.snapshot||!source.openSociety)
        throw std::invalid_argument("License gate requires a snapshot and open action");
    state_->source=std::move(source);state_->openFailure=std::move(openFailure);
    state_->model=model?std::move(model):std::make_shared<iisacc::licensing::SocietyLicenseGateModel>();
    const std::weak_ptr<State> weak=state_;
    std::function<void()> retry;
    if(state_->source.retry)retry=[weak]{if(auto state=weak.lock())state->retry();};
    state_->model->setActions([weak]{if(auto state=weak.lock())state->open();},std::move(retry));
    state_->refresh();
}
NativeLicenseBridge::~NativeLicenseBridge()=default;
std::shared_ptr<iisacc::licensing::SocietyLicenseGateModel> NativeLicenseBridge::model()const{return state_->model;}
void NativeLicenseBridge::refresh(){auto state=state_;state->refresh();}
void NativeLicenseBridge::setOpenFailure(std::string message){
    auto state=state_;const bool showing=!state->actionError.empty();
    state->openFailure=std::move(message);
    if(showing){state->actionError=state->openFailure;state->refresh();}
}
}
