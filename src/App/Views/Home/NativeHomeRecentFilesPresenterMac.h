#pragma once
class QObject;
class ApplicationShutdownCoordinator;
namespace dreamscapes::home {
bool installNativeHomeRecentFilesPresenter(QObject *root, QObject *lifetime, ApplicationShutdownCoordinator *shutdown);
}
