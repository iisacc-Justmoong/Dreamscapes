#pragma once
class ApplicationShutdownCoordinator;
class QObject;
namespace dreamscapes::home {
bool installNativeHomeQueuePresenter(QObject *root, QObject *lifetime, ApplicationShutdownCoordinator *shutdown);
}
