#pragma once
#include <QObject>
#include <QByteArray>
#include <QStringList>
#include <iiSDKCore/Process/Process.h>
#include <memory>

// GUI projection of a Core-owned process session. Native launch, pipe transfer,
// termination/reaping and file leases live on its dispatch worker.
class GenerationProcess final : public QObject {
    Q_OBJECT
public:
    enum ProcessState { NotRunning, Starting, Running };
    Q_ENUM(ProcessState)
    enum ProcessError { FailedToStart, ReadError, WriteError, UnknownError };
    Q_ENUM(ProcessError)
    enum ExitStatus { NormalExit, CrashExit };
    Q_ENUM(ExitStatus)
    explicit GenerationProcess(QObject *parent = nullptr);
    ~GenerationProcess() override;
    ProcessState state() const { return m_status; }
    quint64 processId() const;
    QString errorString() const { return m_error; }
    ii::process::EnvironmentPatch processEnvironment() const { return m_environment; }
    void setProcessEnvironment(ii::process::EnvironmentPatch environment) { m_environment = std::move(environment); }
    void setWorkingDirectory(QString directory) { m_directory = std::move(directory); }
    void start(const QString &program, const QStringList &arguments, std::shared_ptr<void> runtimeLifetime = {});
    qint64 write(const QByteArray &data, std::shared_ptr<void> inputLifetime = {});
    void releaseInput();
    void probeOutput(quint64 token);
    void cancel();
    void retire(std::shared_ptr<void> currentFiles = {});
signals:
    void started();
    void outputAvailable(const QByteArray &data);
    void outputProbed(quint64 token);
    void errorOccurred(GenerationProcess::ProcessError error);
    void finished(qint64 code, GenerationProcess::ExitStatus status);
private:
    struct State;
    void synchronize(const std::shared_ptr<State> &state);
    std::shared_ptr<State> d;
    std::shared_ptr<QObject> m_dispatch;
    ii::process::EnvironmentPatch m_environment;
    QString m_directory, m_error;
    ProcessState m_status = NotRunning;
    bool m_delivering = false;
};
