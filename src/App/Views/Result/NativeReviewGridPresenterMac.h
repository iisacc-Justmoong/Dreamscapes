#pragma once
class QObject;
class ApplicationShutdownCoordinator;
namespace dreamscapes::review {
bool installNativeReviewGridPresenter(QObject *root,QObject *lifetime,ApplicationShutdownCoordinator *shutdown);
}
