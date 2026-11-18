#include "ExternalOverlaySurface.h"
#include <iiGUI>
#include <QCoreApplication>
#include <QTimer>
#include <utility>

namespace dreamscapes::native {
ExternalOverlaySurface::ExternalOverlaySurface(QWindow* window,Predicate wanted,Mount mount,
                                               std::function<void()> unmount)
    :window_(window),wanted_(std::move(wanted)),mount_(std::move(mount)),unmount_(std::move(unmount)){}
ExternalOverlaySurface::~ExternalOverlaySurface()=default;
bool ExternalOverlaySurface::start(std::function<void(bool)> closed){
    if(!window_||!window_->isVisible()||!wanted_||!mount_||!wanted_())return false;
    closed_=std::move(closed);host_=std::make_unique<ii::gui::Host>();
    const auto self=shared_from_this();
    auto attached=host_->attach_external(reinterpret_cast<void*>(window_->winId()),{
        .opened=[self]{self->mount();},.closed=[self]{self->retired();}
    });
    if(!attached){host_.reset();closed_={};return false;}
    return true;
}
void ExternalOverlaySurface::requestClose()noexcept{
    if(!host_||closing_)return;
    closing_=true;const auto weak=weak_from_this();
    auto close=[weak]{if(auto self=weak.lock();self&&self->host_)(void)self->host_->request_close();};
    if(!host_->executor().post(close)&&qApp)QTimer::singleShot(0,qApp,std::move(close));
}
void ExternalOverlaySurface::mount(){
    try {
        if(!host_||closing_||!window_||!window_->isVisible()||!wanted_()){
            requestClose();return;
        }
        if(mount_(*host_))return;
    }catch(...){}
    failed_=true;requestClose();
}
void ExternalOverlaySurface::retired(){
    auto completed=std::move(closed_);
    if(unmount_)try{unmount_();}catch(...){}
    host_.reset();
    if(completed)completed(failed_);
}
}
