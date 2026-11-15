#pragma once
class QObject;
class ApplicationShutdownCoordinator;
namespace dreamscapes::review {
bool installNativeReviewInspectorPresenter(QObject *root,QObject *lifetime,ApplicationShutdownCoordinator *shutdown);
}
