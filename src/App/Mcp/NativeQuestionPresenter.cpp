#include "NativeQuestionPresenter.h"
#include "App/Native/NativeOverlayCoordinator.h"
#include "App/Native/ExternalOverlaySurface.h"
#include "App/ApplicationShutdownCoordinator.h"
#include "UserQuestionsSheet.generated.h"
#include <iiMultimodalAgent>
#include <iiGUI>
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

namespace dreamscapes::questions {
namespace {
namespace presentation = iiMultimodalAgent::agent::presentation;
using View = lvui_generated::UserQuestionsSheet;
using Model = presentation::NativeQuestionViewModel;
using native::NativeOverlayCoordinator;
using native::NativeOverlaySurface;
using native::ExternalOverlaySurface;

presentation::QuestionStrings translatedStrings(){
    const auto text=[](const char* source){return QCoreApplication::translate("UserQuestionsSheet",source).toStdString();};
    presentation::QuestionStrings result;
    result.title=text("Agent questions");
    result.description=text("Choose an option or write your own answer. You can leave questions unanswered.");
    result.waiting=text("%1 requests waiting");
    result.other=text("Your own answer");
    result.notes=text("Additional notes (optional)");
    result.otherAccessibility=text("Your answer to %1");
    result.notesAccessibility=text("Notes for %1");
    result.decline=text("Decline");result.skip=text("Skip");result.send=text("Send answers");
    return result;
}

class NativeQuestionPresenter final : public QObject {
    Q_OBJECT
public:
    NativeQuestionPresenter(QWindow* root,iiMultimodalAgent::agent::QuestionInbox* inbox,QObject* parent)
        :QObject(parent),root_(root),inbox_(inbox),model_(std::make_shared<Model>()){
        model_->setStrings(translatedStrings());
        bridge_=std::make_unique<presentation::NativeQuestionBridge>(inbox,model_);
        const QPointer<NativeQuestionPresenter> self(this);
        coordinator_=std::make_unique<NativeOverlayCoordinator>(
            [window=QPointer<QWindow>(root),model=model_]()->std::shared_ptr<NativeOverlaySurface>{
                if(!window)return {};
                auto scope=std::make_shared<std::shared_ptr<View::Scope>>();
                return std::make_shared<ExternalOverlaySurface>(window,
                    [window,model]{return window&&window->isVisible()&&
                        window->property("licenseWorkspaceVisible").toBool()&&model->open.get();},
                    [model,scope](ii::gui::Host& host){
                        const auto factories=lvui::native::BackendFactories{}.with<presentation::NativeQuestionBackend>(
                            [model]{return presentation::NativeQuestionBackend(model);});
                        return bool(View::mount(host,{},scope.get(),factories));
                    },[scope]{scope->reset();});
            },
            [self](NativeOverlayCoordinator::Task task){
                if(self)QTimer::singleShot(0,self,[task=std::move(task)]{task();});
            },
            [self]{if(self)self->fallback();});
        open_=model_->open.observe([self](bool,std::uint64_t){if(self)self->refreshWanted();});
        connect(root,&QWindow::visibleChanged,this,[self](bool){if(self)self->refreshWanted();});
        connect(root,&QObject::destroyed,this,[self]{if(self)self->shutdown({});});
        root->installEventFilter(this);
        if(qApp)qApp->installEventFilter(this);
    }
    ~NativeQuestionPresenter()override{shutdown({});}
    bool connectLicenseProperty(){
        if(!root_)return false;
        const auto* meta=root_->metaObject();const auto index=meta->indexOfProperty("licenseWorkspaceVisible");
        if(index<0)return false;
        const auto property=meta->property(index);const auto slot=metaObject()->indexOfSlot("refreshWanted()");
        if(!property.hasNotifySignal()||slot<0)return false;
        return bool(QObject::connect(root_,property.notifySignal(),this,metaObject()->method(slot)));
    }
    void begin(){refreshWanted();}
    void shutdown(std::function<void()> completed){
        open_.reset();
        if(coordinator_)coordinator_->shutdown(std::move(completed));else if(completed)completed();
    }
protected:
    bool eventFilter(QObject* watched,QEvent* event)override{
        if((watched==qApp||watched==root_)&&event->type()==QEvent::LanguageChange&&model_)
            model_->setStrings(translatedStrings());
        return QObject::eventFilter(watched,event);
    }
private slots:
    void refreshWanted(){
        if(!coordinator_||!root_||!model_)return;
        coordinator_->setWanted(root_->isVisible()&&root_->property("licenseWorkspaceVisible").toBool()&&model_->open.get());
    }
private:
    void fallback(){
        open_.reset();
        if(root_&&inbox_)root_->setProperty("agentQuestionInbox",QVariant::fromValue(inbox_.data()));
    }
    QPointer<QWindow> root_;
    QPointer<iiMultimodalAgent::agent::QuestionInbox> inbox_;
    std::shared_ptr<Model> model_;
    std::unique_ptr<presentation::NativeQuestionBridge> bridge_;
    std::unique_ptr<NativeOverlayCoordinator> coordinator_;
    ii::core::ObservableSubscription open_;
};
}
bool installNativeQuestionPresenter(QObject* root,iiMultimodalAgent::agent::QuestionInbox* inbox,
                                    QObject* lifetime,ApplicationShutdownCoordinator* shutdown){
    auto* window=qobject_cast<QWindow*>(root);
    if(!window||!inbox||!lifetime||!shutdown||window->thread()!=QThread::currentThread()||
       inbox->thread()!=QThread::currentThread()||lifetime->thread()!=QThread::currentThread()||
       !shutdown->isOwnerThread()||shutdown->isClosing())return false;
    try{
        auto presenter=std::make_unique<NativeQuestionPresenter>(window,inbox,lifetime);
        if(!presenter->connectLicenseProperty())return false;
        const QPointer<NativeQuestionPresenter> guard(presenter.get());
        if (!shutdown->add([guard](std::function<void()> done){if(guard)guard->shutdown(std::move(done));else if(done)done();})) return false;
        presenter->begin();
        presenter.release();return true;
    }catch(...){return false;}
}
}
#include "NativeQuestionPresenter.moc"
