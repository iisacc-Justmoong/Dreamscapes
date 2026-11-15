#pragma once
class ApplicationShutdownCoordinator;
class QObject;
namespace dreamscapes::native {
// Admits only an owner-thread window and the product's existing license source.
// The legacy gate remains underneath if a native host cannot be attached.
[[nodiscard]] bool installNativeLicensePresenter(QObject* root,QObject* account,
    QObject* lifetime,ApplicationShutdownCoordinator* shutdown);
}
