#pragma once
class ApplicationShutdownCoordinator;
class QObject;
namespace iiMultimodalAgent::agent { class QuestionInbox; }

namespace dreamscapes::questions {
// Installs the event-driven native question surface on a supported desktop
// host. The caller keeps the legacy transport available if admission fails.
[[nodiscard]] bool installNativeQuestionPresenter(QObject* root,
    iiMultimodalAgent::agent::QuestionInbox* inbox, QObject* lifetime,
    ApplicationShutdownCoordinator* shutdown);
}
