#pragma once
class QObject;
class ApplicationShutdownCoordinator;
void installDreamscapesMcp(QObject* root, QObject* lifetime, ApplicationShutdownCoordinator* shutdown);
