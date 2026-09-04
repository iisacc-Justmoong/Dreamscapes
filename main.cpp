#include <backend/runtime/appentry.h>

int main(int argc, char *argv[])
{
    lvrs::QmlAppLaunchSpec application;
    application.bootstrap.applicationName = QStringLiteral("Dreamscapes");
    application.bootstrap.quickStyleName = QStringLiteral("Basic");
    application.moduleUri = QStringLiteral("Dreamscapes");
    application.rootObject = QStringLiteral("Main");

    return lvrs::runBootstrappedQmlApp(argc, argv, application);
}
