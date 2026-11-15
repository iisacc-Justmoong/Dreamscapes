#include "App/Native/NativeLicensePresenter.h"
#include "App/ApplicationShutdownCoordinator.h"
#include <QEvent>
#include <QGuiApplication>
#include <QWindow>
#include <stdexcept>

class LicenseWindow final : public QWindow {
    Q_OBJECT
    Q_PROPERTY(bool licenseWorkspaceVisible READ workspaceVisible NOTIFY workspaceChanged)
public:
    bool workspaceVisible()const{return visible_;}
    void setWorkspaceVisible(bool value){if(visible_!=value){visible_=value;emit workspaceChanged();}}
signals:
    void workspaceChanged();
private:
    bool visible_=false;
};
class Account final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(QString code READ code NOTIFY changed)
public:
    bool checking()const{return checking_;}
    QString code()const{return QStringLiteral("license_not_owned");}
    Q_INVOKABLE bool openSociety(){return true;}
    void refresh(){checking_=true;emit changed();}
signals:
    void changed();
private:
    bool checking_=false;
};
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);ApplicationShutdownCoordinator shutdown;
    QObject lifetime,invalidRoot,invalidAccount;LicenseWindow root;Account account;QWindow noLicenseProperty;
    using dreamscapes::native::installNativeLicensePresenter;
    check(!installNativeLicensePresenter(&invalidRoot,&account,&lifetime,&shutdown),"A non-window root was admitted");
    check(!installNativeLicensePresenter(&root,&invalidAccount,&lifetime,&shutdown),"An unsupported account was admitted");
    check(!installNativeLicensePresenter(&noLicenseProperty,&account,&lifetime,&shutdown),"A root without a license notification was admitted");
    check(installNativeLicensePresenter(&root,&account,&lifetime,&shutdown),"The hidden window was not admitted");
    root.setWorkspaceVisible(true);account.refresh();root.setWorkspaceVisible(false);
    QEvent language(QEvent::LanguageChange);QCoreApplication::sendEvent(&root,&language);
    QCoreApplication::processEvents();check(!root.isVisible(),"A native application window was shown");
    QEvent quit(QEvent::Quit);QCoreApplication::sendEvent(&app,&quit);
    check(shutdown.isClosing(),"Application shutdown was not coordinated");
    check(!installNativeLicensePresenter(&root,&account,&lifetime,&shutdown),"A closing application admitted a presenter");
    QCoreApplication::processEvents();
}
#include "presenter.moc"
