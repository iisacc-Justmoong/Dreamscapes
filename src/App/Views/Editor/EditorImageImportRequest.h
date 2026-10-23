#pragma once
#include "EditorImageImport.h"
#include <Container.h>
#include <QFutureWatcher>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <optional>

class EditorProject;
class EditorCanvas;
Q_MOC_INCLUDE("App/Views/Editor/EditorProject.h")
Q_MOC_INCLUDE("App/Views/Editor/EditorCanvas.h")

// Accepted editor command, separate from both original hydration and document
// ownership. The worker receives detached values; only this GUI owner may adopt.
class EditorImageImportRequest : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorProject *project READ project WRITE setProject NOTIFY bindingsChanged)
    Q_PROPERTY(iiSocietyContainer::Container *container READ container WRITE setContainer NOTIFY bindingsChanged)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit EditorImageImportRequest(QObject *parent=nullptr);
    EditorImageImportRequest(dreamscapes::ImageImportOperation operation,QObject *parent);
    ~EditorImageImportRequest() override;
    EditorProject *project() const;
    iiSocietyContainer::Container *container() const {return m_container;}
    void setProject(EditorProject *project);
    void setContainer(iiSocietyContainer::Container *container);
    bool active() const {return m_active||m_adopting;}
    QString error() const {return m_error;}
    Q_INVOKABLE bool start(const QVariantList &sources,const QVariantList &entries={},bool attached=false);
    Q_INVOKABLE void cancel();
signals:
    void bindingsChanged();
    void changed();
    void ready(QVariantList entries,QVariantList sources,bool attached);
    void failed(QString message);
    void cancelled();
    void rejected(QString message);
private:
    struct Job {
        dreamscapes::ImageImportRequest request;
        QVariantList sources,entries,aliases;
        QPointer<EditorProject> project;
        QPointer<EditorCanvas> canvas;
        QPointer<iiSocietyContainer::Container> container;
        QString path,containerId,containerRoot;
        qulonglong revision=0,epoch=0;
        bool attached=false;
    };
    void bindCanvas();
    void stop(const QString &message,bool failure);
    void launch();
    void complete();
    bool sourceContextMatches(const Job &job) const;
    const Job *current() const;
    QPointer<EditorProject> m_project;
    QPointer<EditorCanvas> m_canvas;
    QPointer<iiSocietyContainer::Container> m_container;
    dreamscapes::ImageImportOperation m_operation;
    QFutureWatcher<dreamscapes::ImageImportResult> m_completion;
    std::optional<Job> m_running,m_pending;
    qulonglong m_epoch=0,m_projectBindingEpoch=0,m_containerBindingEpoch=0;
    QString m_error;
    bool m_active=false,m_adopting=false,m_adoptionCancelled=false;
};
