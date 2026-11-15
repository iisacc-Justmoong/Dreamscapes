#include "NativeLicensePresenter.h"
#include "ExternalOverlaySurface.h"
#include "NativeLicenseBridge.h"
#include "NativeOverlayCoordinator.h"
#include "App/ApplicationShutdownCoordinator.h"
#include "SocietyLicenseGate.generated.h"
#include <iiGUI/Host.h>
#include <QCoreApplication>
#include <QEvent>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QWindow>
#include <memory>
#include <utility>

namespace dreamscapes::native {
namespace {
using View=lvui_generated::SocietyLicenseGate;
using Model=iisacc::licensing::SocietyLicenseGateModel;

iisacc::licensing::GateStrings translatedStrings(){
    const auto text=[](const char* value){return QCoreApplication::translate("SocietyLicenseGate",value).toStdString();};
    iisacc::licensing::GateStrings s;
    s.title=text("Connect to Society");s.subtitle=text("Verify access before opening %1.");
    s.description=text("Society verifies the license for this device and account.");
    s.open=text("Open Society");s.retry=text("Restart Dreamscapes to check again");s.checkingButton=text("Checking…");
    s.checking=text("Checking your license through Society…");
    s.notOwned=text("Your Society account does not have a %1 license.");
    s.deviceLimit=text("This device limit was reached. Manage your devices in Society.");
    s.invalidResponse=text("The license response could not be verified. Check Society and try again.");
    s.deviceUnavailable=text("This device could not be identified. Try again on a supported device.");
    s.fallback=text("Open Society and sign in, then restart Dreamscapes.");
    return s;
}
std::string openFailure(){return QCoreApplication::translate("Main","Could not open Society. Open it on this device and try again.").toStdString();}

class NativeLicensePresenter final : public QObject {
    Q_OBJECT
public:
    NativeLicensePresenter(QWindow* window,QObject* account,QObject* parent)
        :QObject(parent),window_(window),account_(account){
        const QPointer<NativeLicensePresenter> self(this);
        coordinator_=std::make_unique<NativeOverlayCoordinator>(
            [self,window=QPointer<QWindow>(window)]()->std::shared_ptr<NativeOverlaySurface>{
                if(!self||!window)return {};
                auto scope=std::make_shared<std::shared_ptr<View::Scope>>();
                return std::make_shared<ExternalOverlaySurface>(window,
                    [self,window]{return self&&window&&window->isVisible()&&self->account_&&
                        !window->property("licenseWorkspaceVisible").toBool();},
                    [self,scope](ii::gui::Host& host){
                        if(!self||!View::mount(host,{},scope.get())||!*scope)return false;
                        auto model=std::shared_ptr<Model>(*scope,&(*scope)->backend_0);
                        self->attachModel(std::move(model));return bool(self);
                    },
                    [self,scope]{if(self)self->bridge_.reset();scope->reset();});
            },
            [self](NativeOverlayCoordinator::Task task){
                if(self)QTimer::singleShot(0,self,[task=std::move(task)]{task();});
            },[]{}); // The isolated QML gate remains available underneath.
        connect(window,&QWindow::visibleChanged,this,[self](bool){if(self)self->refreshWanted();});
        connect(window,&QObject::destroyed,this,[self]{if(self)self->shutdown({});});
        connect(account,&QObject::destroyed,this,[self]{if(self)self->shutdown({});});
        window->installEventFilter(this);if(qApp)qApp->installEventFilter(this);
    }
    ~NativeLicensePresenter()override{shutdown({});}
    bool connectState(){
        if(!window_||!account_)return false;
        const auto slot=metaObject()->indexOfSlot("refreshWanted()");
        const auto accountSlot=metaObject()->indexOfSlot("refreshAccount()");
        const auto* wm=window_->metaObject();const auto wi=wm->indexOfProperty("licenseWorkspaceVisible");
        const auto* am=account_->metaObject();const auto checking=am->indexOfProperty("checking");
        const auto code=am->indexOfProperty("code");
        if(slot<0||accountSlot<0||wi<0||checking<0||code<0||
           am->indexOfMethod("openSociety()")<0)return false;
        const auto wp=wm->property(wi),cp=am->property(checking),sp=am->property(code);
        if(!wp.hasNotifySignal()||!cp.hasNotifySignal()||!sp.hasNotifySignal())return false;
        if(!QObject::connect(window_,wp.notifySignal(),this,metaObject()->method(slot))||
           !QObject::connect(account_,cp.notifySignal(),this,metaObject()->method(accountSlot)))return false;
        if(cp.notifySignalIndex()!=sp.notifySignalIndex()&&
           !QObject::connect(account_,sp.notifySignal(),this,metaObject()->method(accountSlot)))return false;
        return true;
    }
    void begin(){refreshWanted();}
    void shutdown(std::function<void()> completed){
        if(coordinator_)coordinator_->shutdown(std::move(completed));else if(completed)completed();
    }
protected:
    bool eventFilter(QObject* watched,QEvent* event)override{
        if((watched==qApp||watched==window_)&&event->type()==QEvent::LanguageChange)translate();
        return QObject::eventFilter(watched,event);
    }
private slots:
    void refreshWanted(){
        if(coordinator_&&window_&&account_)
            coordinator_->setWanted(window_->isVisible()&&!window_->property("licenseWorkspaceVisible").toBool());
    }
    void refreshAccount(){if(bridge_)bridge_->refresh();}
private:
    void attachModel(std::shared_ptr<Model> model){
        const QPointer<QObject> account=account_;
        LicenseGateSource source{
            [account]{
                iisacc::licensing::GateState result;result.productName=QCoreApplication::applicationName().toStdString();
                if(account){result.checking=account->property("checking").toBool();result.statusCode=account->property("code").toString().toStdString();}
                return result;
            },
            [account]{bool ok=false;return account&&QMetaObject::invokeMethod(account,"openSociety",Qt::DirectConnection,Q_RETURN_ARG(bool,ok))&&ok;},
            {} // A decision is made once per application launch.
        };
        bridge_=std::make_unique<NativeLicenseBridge>(std::move(source),openFailure(),std::move(model));
        translate();
    }
    void translate(){if(bridge_){bridge_->setOpenFailure(openFailure());bridge_->model()->setStrings(translatedStrings());}}
    QPointer<QWindow> window_;
    QPointer<QObject> account_;
    std::unique_ptr<NativeOverlayCoordinator> coordinator_;
    std::unique_ptr<NativeLicenseBridge> bridge_;
};
}
bool installNativeLicensePresenter(QObject* root,QObject* account,QObject* lifetime,ApplicationShutdownCoordinator* shutdown){
    auto* window=qobject_cast<QWindow*>(root);
    if(!window||!account||!lifetime||!shutdown||window->thread()!=QThread::currentThread()||
       account->thread()!=QThread::currentThread()||lifetime->thread()!=QThread::currentThread()||
       shutdown->thread()!=QThread::currentThread()||shutdown->isClosing())return false;
    try{
        auto presenter=std::make_unique<NativeLicensePresenter>(window,account,lifetime);
        if(!presenter->connectState())return false;
        const QPointer<NativeLicensePresenter> guard(presenter.get());
        shutdown->add([guard](std::function<void()> done){if(guard)guard->shutdown(std::move(done));else if(done)done();});
        presenter->begin();presenter.release();return true;
    }catch(...){return false;}
}
}
#include "NativeLicensePresenter.moc"
