#pragma once
class QObject;
class ApplicationShutdownCoordinator;
namespace dreamscapes::home {
bool installNativeHomeRecentGenerationsPresenter(QObject *root, QObject *lifetime, ApplicationShutdownCoordinator *shutdown);
}
