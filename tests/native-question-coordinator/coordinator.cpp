#include "App/Native/NativeOverlayCoordinator.h"
#include <deque>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace dreamscapes::native;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Queue final {
    std::deque<std::function<void()>> pending;
    void post(std::function<void()> task){pending.push_back(std::move(task));}
    void drain(){for(int n=0;!pending.empty();++n){check(n<32,"Overlay scheduled an endless loop");auto task=std::move(pending.front());pending.pop_front();task();}}
};
struct Surface final : NativeOverlaySurface {
    int starts=0,closes=0;
    bool accepted=true;
    bool starting=false,closedDuringStart=false;
    std::function<void()> onStart;
    std::function<void(bool)> closed;
    bool start(std::function<void(bool)> callback)override{++starts;closed=std::move(callback);starting=true;if(onStart)onStart();starting=false;return accepted;}
    void requestClose()noexcept override{++closes;closedDuringStart|=starting;}
    void finish(bool failed=false){if(closed)closed(failed);}
};
void lifecycle(){
    Queue queue;std::vector<std::shared_ptr<Surface>> surfaces;int fallback=0;
    NativeOverlayCoordinator owner([&]{auto s=std::make_shared<Surface>();surfaces.push_back(s);return s;},
        [&](auto task){queue.post(std::move(task));},[&]{++fallback;});
    check(queue.pending.empty()&&surfaces.empty(),"An idle overlay allocated work");
    owner.setWanted(true);owner.setWanted(true);
    check(queue.pending.size()==1&&surfaces.empty(),"Overlay did not coalesce the open event");
    queue.drain();check(surfaces.size()==1&&surfaces[0]->starts==1,"Overlay did not start exactly once");
    auto first=surfaces[0];auto stale=first->closed;
    owner.setWanted(false);queue.drain();check(first->closes==1,"Closing the request did not close its surface");
    owner.setWanted(true);queue.drain();check(surfaces.size()==1,"An opening overtook the previous surface close");
    first->finish();queue.drain();check(surfaces.size()==2&&surfaces[1]->starts==1,"A new request was lost while closing");
    stale(false);queue.drain();check(surfaces.size()==2&&surfaces[1]->closes==0,"A stale close affected a newer session");
    surfaces[1]->finish(true);queue.drain();
    check(fallback==1&&surfaces.size()==2,"An asynchronous host failure did not fall back exactly once");
    owner.setWanted(false);owner.setWanted(true);queue.drain();check(fallback==1&&surfaces.size()==2,"Failure caused a retry loop");
}
void shutdown(){
    Queue queue;std::shared_ptr<Surface> surface;int completed=0;
    NativeOverlayCoordinator owner([&]{surface=std::make_shared<Surface>();return surface;},
        [&](auto task){queue.post(std::move(task));},[]{});
    owner.setWanted(true);queue.drain();owner.shutdown([&]{++completed;});
    check(surface->closes==1&&completed==0,"Shutdown completed before the surface retired");
    owner.setWanted(true);queue.drain();surface->finish();queue.drain();
    check(completed==1&&surface->starts==1,"Shutdown did not retire exactly one session");
    owner.shutdown([&]{++completed;});check(completed==2,"Repeated shutdown lost its completion");
}
void failureAndDestruction(){
    Queue queue;int fallback=0;std::shared_ptr<Surface> surface;
    {
        NativeOverlayCoordinator owner([&]{surface=std::make_shared<Surface>();surface->accepted=false;return surface;},
            [&](auto task){queue.post(std::move(task));},[&]{++fallback;});
        owner.setWanted(true);queue.drain();check(fallback==1&&surface->starts==1,"Synchronous host failure was not reported");
        owner.setWanted(true);queue.drain();check(surface->starts==1,"A failed host retried without a request");
    }
    {
        NativeOverlayCoordinator owner([&]{surface=std::make_shared<Surface>();return surface;},
            [&](auto task){queue.post(std::move(task));},[]{});
        owner.setWanted(true);queue.drain();
    }
    check(surface->closes==1,"Destruction did not request surface retirement");
    surface->finish();queue.drain();
}
void reentrantStart(){
    std::shared_ptr<Surface> surface;NativeOverlayCoordinator* active=nullptr;int completed=0;
    NativeOverlayCoordinator owner([&]{surface=std::make_shared<Surface>();surface->onStart=[&]{active->shutdown([&]{++completed;});};return surface;},
        [](auto task){task();},[]{});
    active=&owner;owner.setWanted(true);
    check(surface->closes==1&&!surface->closedDuringStart&&completed==0,
          "Reentrant shutdown closed a surface before start returned");
    surface->finish();check(completed==1,"Reentrant shutdown did not wait for retirement");
}
void failedScheduler(){
    Queue queue;std::shared_ptr<Surface> surface;bool reject=false;int fallback=0;
    NativeOverlayCoordinator owner([&]{surface=std::make_shared<Surface>();return surface;},
        [&](auto task){if(reject)throw std::runtime_error("scheduler stopped");queue.post(std::move(task));},[&]{++fallback;});
    owner.setWanted(true);queue.drain();reject=true;owner.setWanted(false);
    check(surface->closes==1&&fallback==0,"Scheduler failure exposed fallback before retiring the surface");
    surface->finish();check(fallback==1,"Scheduler failure lost its fallback after retirement");
}
}
int main(){lifecycle();shutdown();failureAndDestruction();reentrantStart();failedScheduler();}
