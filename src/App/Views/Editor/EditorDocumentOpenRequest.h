#pragma once
#include "EditorDocumentOpen.h"
#include <Container.h>
#include <QFutureWatcher>
#include <QPointer>
#include <QUrl>
#include <optional>

class EditorProject;
class EditorCanvas;
Q_MOC_INCLUDE("App/Views/Editor/EditorProject.h")
Q_MOC_INCLUDE("App/Views/Editor/EditorCanvas.h")

// GUI command owner. Exactly one running read and one latest pending intent;
// the operation receives values and never captures a GUI object.
class EditorDocumentOpenRequest : public QObject {
    Q_OBJECT
public:
    explicit EditorDocumentOpenRequest(QObject *parent=nullptr);
    EditorDocumentOpenRequest(dreamscapes::DocumentOpenOperation operation,QObject *parent);
    ~EditorDocumentOpenRequest() override;
    void setProject(EditorProject *project);
    void setContainer(iiSocietyContainer::Container *container);
    iiSocietyContainer::Container *container() const {return m_container;}
    bool active() const {return m_active||m_adopting;}
    QString error() const {return m_error;}
    qulonglong generation() const {return m_epoch;}
    bool start(const QUrl &source,bool asCopy=false);
    void cancel();
signals:
    void changed();
    void ready(QUrl source);
    void failed(QString message);
    void cancelled();
    void rejected(QString message);
private:
    struct Job {
        dreamscapes::DocumentOpenInput input;
        QUrl source;
        QPointer<EditorProject> project;
        QPointer<EditorCanvas> canvas;
        QPointer<iiSocietyContainer::Container> container;
        QString currentPath,id,root;
        qulonglong revision=0,epoch=0;
        bool hadContainer=false,available=false;
    };
    void bindCanvas();
    void stop(const QString &message,bool failure);
    void launch();
    void complete();
    bool contextMatches(const Job &job) const;
    QPointer<EditorProject> m_project;
    QPointer<EditorCanvas> m_canvas;
    QPointer<iiSocietyContainer::Container> m_container;
    dreamscapes::DocumentOpenOperation m_operation;
    QFutureWatcher<dreamscapes::DocumentOpenResult> m_completion;
    std::optional<Job> m_running,m_pending;
    qulonglong m_epoch=0;
    QString m_error;
    bool m_active=false,m_adopting=false,m_adoptionCancelled=false;
};
