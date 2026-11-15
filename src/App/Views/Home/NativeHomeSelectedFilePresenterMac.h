#pragma once
class ApplicationShutdownCoordinator;
class QObject;
namespace dreamscapes::home {
bool installNativeHomeSelectedFilePresenter(QObject *root, QObject *lifetime, ApplicationShutdownCoordinator *shutdown);
}
