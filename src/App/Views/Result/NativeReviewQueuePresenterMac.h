#pragma once
class ApplicationShutdownCoordinator;
class QObject;
namespace dreamscapes::review {
// Installs the macOS native queue into the current window when its existing
// product sessions and the temporary host regions are available.
bool installNativeReviewQueuePresenter(QObject *root, QObject *lifetime, ApplicationShutdownCoordinator *shutdown);
}
