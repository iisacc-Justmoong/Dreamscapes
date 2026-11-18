#include "App/Mcp/NativeQuestionPresenter.h"
#include "App/ApplicationShutdownCoordinator.h"
#include <iiSDKCore>
#include <iiMultimodalAgent>
#include <QEvent>
#include <QGuiApplication>
#include <QWindow>
#include <stdexcept>

// Never create or show a native window. The offscreen Qt platform is used only
// to exercise QObject admission, license notifications, and orderly shutdown.
class LicensedWindow final : public QWindow {
    Q_OBJECT
    Q_PROPERTY(bool licenseWorkspaceVisible READ workspaceVisible NOTIFY workspaceVisibleChanged)
public:
    bool workspaceVisible() const { return visible_; }
    void setWorkspaceVisible(bool visible) {
        if(visible_==visible)return;
        visible_=visible;emit workspaceVisibleChanged();
    }
signals:
    void workspaceVisibleChanged();
private:
    bool visible_=false;
};

static void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}

int main(int argc,char** argv){
    QGuiApplication app(argc,argv);
    ii::runtime::EventLoop eventLoop;
    ApplicationShutdownCoordinator shutdown(eventLoop);
    iiMultimodalAgent::agent::QuestionInbox inbox;
    QObject lifetime;
    QObject invalidRoot;
    QWindow noLicenseProperty;
    LicensedWindow root;
    using dreamscapes::questions::installNativeQuestionPresenter;
    require(!installNativeQuestionPresenter(&invalidRoot,&inbox,&lifetime,&shutdown),"A non-window root was admitted");
    require(!installNativeQuestionPresenter(&root,nullptr,&lifetime,&shutdown),"A missing inbox was admitted");
    require(!installNativeQuestionPresenter(&root,&inbox,nullptr,&shutdown),"A missing lifetime was admitted");
    require(!installNativeQuestionPresenter(&root,&inbox,&lifetime,nullptr),"A missing shutdown coordinator was admitted");
    require(!installNativeQuestionPresenter(&noLicenseProperty,&inbox,&lifetime,&shutdown),"A root without a license notification was admitted");
    require(installNativeQuestionPresenter(&root,&inbox,&lifetime,&shutdown),"The valid hidden window was not admitted");
    root.setWorkspaceVisible(true);
    root.setWorkspaceVisible(false);
    QEvent languageChange(QEvent::LanguageChange);
    QCoreApplication::sendEvent(&root,&languageChange);
    QCoreApplication::processEvents();
    require(!root.isVisible(),"The presenter must not show the application window");
    require(bool(shutdown.begin()),"Native shutdown could not start");
    require(shutdown.isClosing(),"The presenter did not participate in application shutdown");
    require(!installNativeQuestionPresenter(&root,&inbox,&lifetime,&shutdown),"A closing application admitted a new presenter");
    QCoreApplication::processEvents();
    (void)eventLoop.poll();
}

#include "presenter.moc"
