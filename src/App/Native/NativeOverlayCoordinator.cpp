#include "NativeOverlayCoordinator.h"
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dreamscapes::native {
struct NativeOverlayCoordinator::State final : std::enable_shared_from_this<State> {
    Factory factory;
    Scheduler scheduler;
    Task fallback;
    std::shared_ptr<NativeOverlaySurface> surface;
    std::vector<Task> completions;
    std::uint64_t generation=0;
    bool wanted=false,scheduled=false,starting=false,closing=false,stopped=false,failed=false;

    void finishShutdown(){
        if(!stopped||surface||starting)return;
        auto callbacks=std::exchange(completions,{});
        for(auto& callback:callbacks)if(callback)try{callback();}catch(...){}
    }
    void deliverFallback(){
        if(stopped){finishShutdown();return;}
        if(!failed||surface||starting)return;
        auto callback=std::move(fallback);
        if(callback)callback();
    }
    void fail(){
        if(stopped){finishShutdown();return;}
        failed=true;wanted=false;factory={};
        if(surface){
            if(!closing&&!starting){closing=true;auto active=surface;active->requestClose();}
            return;
        }
        deliverFallback();
    }
    void schedule(){
        if(scheduled||stopped||failed)return;
        scheduled=true;auto weak=weak_from_this();
        try{scheduler([weak]{if(auto self=weak.lock()){self->scheduled=false;self->reconcile();}});}
        catch(...){scheduled=false;fail();}
    }
    void closed(std::uint64_t token,bool error){
        if(token!=generation||!surface)return;
        const bool requested=closing;
        surface.reset();closing=false;
        if(stopped){finishShutdown();return;}
        if(failed){deliverFallback();return;}
        if(error||(!requested&&wanted)){fail();return;}
        if(wanted)schedule();
    }
    void reconcile(){
        if(stopped){finishShutdown();return;}
        if(failed){deliverFallback();return;}
        if(surface){
            if(starting)return;
            if(!wanted&&!closing){closing=true;auto active=surface;active->requestClose();}
            return;
        }
        if(!wanted)return;
        std::shared_ptr<NativeOverlaySurface> next;
        auto make=factory;
        try{next=make();}catch(...){fail();return;}
        if(!next){fail();return;}
        if(stopped||failed||!wanted){finishShutdown();return;}
        surface=next;const auto token=++generation;auto self=shared_from_this();starting=true;
        bool accepted=false;
        try{accepted=next->start([self,token](bool error){self->closed(token,error);});}catch(...){accepted=false;}
        starting=false;
        if(token!=generation||surface!=next){if(stopped)finishShutdown();else deliverFallback();return;}
        if(!accepted){surface.reset();fail();return;}
        // A start callback can reenter setWanted or shutdown. Act only after
        // the surface has completed its own admission path.
        if(stopped||failed){if(!closing){closing=true;next->requestClose();}return;}
        if(!wanted)schedule();
    }
    void shutdown(Task completed){
        if(completed)completions.push_back(std::move(completed));
        stopped=true;wanted=false;
        if(!surface){finishShutdown();return;}
        if(!closing&&!starting){closing=true;auto active=surface;active->requestClose();}
    }
};
NativeOverlayCoordinator::NativeOverlayCoordinator(Factory factory,Scheduler scheduler,Task fallback)
    :state_(std::make_shared<State>()){
    if(!factory||!scheduler||!fallback)throw std::invalid_argument("Native overlay requires a factory, scheduler and fallback");
    state_->factory=std::move(factory);state_->scheduler=std::move(scheduler);state_->fallback=std::move(fallback);
}
NativeOverlayCoordinator::~NativeOverlayCoordinator(){if(state_)state_->shutdown({});}
void NativeOverlayCoordinator::setWanted(bool wanted){
    auto state=state_;if(state->stopped||state->failed||state->wanted==wanted)return;
    state->wanted=wanted;state->schedule();
}
void NativeOverlayCoordinator::shutdown(Task completed){state_->shutdown(std::move(completed));}
}
