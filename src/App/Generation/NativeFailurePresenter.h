#pragma once

#include "App/Views/Failure/NativeFailureDetails.h"
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

class GenerationController;

namespace dreamscapes::failure {
// Transitional adapter: the model and LVUI view remain independent of Qt.
// The existing application owns the parent window and its event loop.
class NativeFailurePresenter final : public QObject {
    Q_OBJECT
public:
    explicit NativeFailurePresenter(QObject *parent = nullptr);
    ~NativeFailurePresenter() override;

    Q_INVOKABLE bool open(QObject *controller, QObject *window, const QString &jobId);
    [[nodiscard]] bool openOnNativeParent(GenerationController *, void *nativeParent, const QString &jobId);
    [[nodiscard]] std::shared_ptr<FailureDetailsModel> activeModel() const;
    void shutdown(std::function<void()> done);

signals:
    void dismissed(const QString &jobId);
    void fallbackRequired(const QString &jobId);

private:
    struct Session;
    std::shared_ptr<Session> session_;
};
} // namespace dreamscapes::failure
