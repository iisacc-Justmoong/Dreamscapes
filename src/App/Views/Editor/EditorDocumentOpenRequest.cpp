#include "EditorDocumentOpenRequest.h"
#include "EditorProject.h"
#include <QtConcurrentRun>

EditorDocumentOpenRequest::EditorDocumentOpenRequest(QObject *parent)
    :EditorDocumentOpenRequest(dreamscapes::prepareDocumentOpen,parent) {}
EditorDocumentOpenRequest::EditorDocumentOpenRequest(dreamscapes::DocumentOpenOperation operation,QObject *parent)
    :QObject(parent),m_operation(std::move(operation)) {
    connect(&m_completion,&QFutureWatcher<dreamscapes::DocumentOpenResult>::finished,this,&EditorDocumentOpenRequest::complete);
}
EditorDocumentOpenRequest::~EditorDocumentOpenRequest() {
    ++m_epoch;m_active=false;
    if(m_running)m_running->input.cancelled->store(true);
    if(m_pending)m_pending->input.cancelled->store(true);
    disconnect(&m_completion,nullptr,this,nullptr);
}
void EditorDocumentOpenRequest::setProject(EditorProject *value) {
    if(m_project==value)return;
    if(m_project)disconnect(m_project,nullptr,this,nullptr);
    m_project=value;bindCanvas();
    if(value) {
        connect(value,&EditorProject::currentCanvasChanged,this,[this] {
            const QPointer<EditorDocumentOpenRequest> guard(this);
            if(m_active)stop(tr("The document changed while opening. Open the file again."),true);
            if(guard)bindCanvas();
        });
        connect(value,&QObject::destroyed,this,[this]{if(m_active)stop(tr("The document project is unavailable."),true);});
    }
    // Cancellation callbacks must see the newly selected target. No writes
    // follow the callback, so a reentrant newer binding/intent remains current.
    cancel();
}
void EditorDocumentOpenRequest::bindCanvas() {
    if(m_canvas)disconnect(m_canvas,nullptr,this,nullptr);
    m_canvas=m_project?m_project->currentCanvas():nullptr;
    if(!m_canvas)return;
    const auto changed=[this]{if(m_active)stop(tr("The document changed while opening. Open the file again."),true);};
    connect(m_canvas,&EditorCanvas::revisionChanged,this,changed);
    connect(m_canvas,&EditorCanvas::documentChanged,this,changed);
    connect(m_canvas,&EditorCanvas::liveStrokeActiveChanged,this,[this,changed]{if(m_canvas&&m_canvas->liveStrokeActive())changed();});
    connect(m_canvas,&EditorCanvas::fileStateChanged,this,[this,changed]{
        const auto *job=m_pending?&*m_pending:m_running?&*m_running:nullptr;
        if(job&&m_canvas&&job->currentPath!=m_canvas->filePath())changed();
    });
    connect(m_canvas,&QObject::destroyed,this,changed);
}
void EditorDocumentOpenRequest::setContainer(iiSocietyContainer::Container *value) {
    if(m_container==value)return;
    if(m_container)disconnect(m_container,nullptr,this,nullptr);
    m_container=value;
    if(value) {
        connect(value,&iiSocietyContainer::Container::stateChanged,this,[this] {
            const auto *job=m_pending?&*m_pending:m_running?&*m_running:nullptr;
            if(m_active&&job&&!contextMatches(*job))stop(tr("The selected drive changed while opening. Open the file again."),true);
        });
        connect(value,&QObject::destroyed,this,[this]{if(m_active)stop(tr("The selected drive is unavailable."),true);});
    }
    cancel();
}
bool EditorDocumentOpenRequest::contextMatches(const Job &job) const {
    if(!job.hadContainer)return !m_container;
    return job.container&&job.container==m_container&&job.container->available()==job.available
        &&job.container->containerId()==job.id&&job.container->rootPath()==job.root;
}
bool EditorDocumentOpenRequest::start(const QUrl &source,bool asCopy) {
    const auto reject=[this](const QString &message){emit rejected(message);return false;};
    if(m_adopting)return reject(tr("The previous document is being applied."));
    if(!m_project||!m_operation)return reject(tr("The document project is unavailable."));
    const auto path=source.isLocalFile()?source.toLocalFile():source.scheme().isEmpty()?source.toString():QString{};
    if(path.isEmpty()||path.contains(QChar::Null))return reject(tr("Choose a local canvas or project."));
    Job job;job.project=m_project;job.canvas=m_project->currentCanvas();job.container=m_container;job.hadContainer=bool(m_container);
    if(!job.canvas||job.canvas->liveStrokeActive())return reject(tr("Finish the current stroke before opening a document."));
    job.currentPath=job.canvas->filePath();job.revision=job.canvas->revision();job.source=source;
    if(m_container){job.id=m_container->containerId();job.root=m_container->rootPath();job.available=m_container->available();}
    job.input={std::filesystem::path(path.toStdU16String()),std::filesystem::path(job.root.toStdU16String()),std::make_shared<std::atomic_bool>(false),asCopy};
    const auto before=m_epoch;const bool retired=m_active||m_pending.has_value();
    const QPointer<EditorDocumentOpenRequest> guard(this);cancel();
    if(!guard||m_epoch!=before+(retired?1:0))return true;
    if(!job.project||job.project!=m_project||job.canvas!=m_project->currentCanvas()||job.canvas->revision()!=job.revision
        ||job.canvas->filePath()!=job.currentPath||job.canvas->liveStrokeActive()||!contextMatches(job))
        return reject(tr("The document or selected drive changed. Open the file again."));
    job.epoch=++m_epoch;m_pending=std::move(job);m_active=true;m_error.clear();launch();emit changed();return true;
}
void EditorDocumentOpenRequest::stop(const QString &message,bool failure) {
    const bool active=m_active;m_active=false;const auto epoch=++m_epoch;
    if(m_running)m_running->input.cancelled->store(true);
    if(m_pending)m_pending->input.cancelled->store(true);
    m_pending.reset();m_error=message;
    if(!active)return;
    const QPointer<EditorDocumentOpenRequest> guard(this);
    if(failure)emit failed(message);else emit cancelled();
    if(guard&&m_epoch==epoch&&!m_active)emit changed();
}
void EditorDocumentOpenRequest::cancel() {
    if(m_adopting) {
        if(!m_adoptionCancelled){m_adoptionCancelled=true;++m_epoch;emit cancelled();}
    } else if(m_active||m_pending)stop({},false);
}
void EditorDocumentOpenRequest::launch() {
    if(m_running||!m_pending)return;
    m_running=std::move(m_pending);m_pending.reset();
    m_completion.setFuture(QtConcurrent::run([operation=m_operation,input=m_running->input] {
        try{return operation(input);}
        catch(const std::exception &error){dreamscapes::DocumentOpenResult result;result.error=error.what();return result;}
        catch(...){dreamscapes::DocumentOpenResult result;result.error="The document could not be opened.";return result;}
    }));
}
void EditorDocumentOpenRequest::complete() {
    if(!m_running)return;
    auto job=std::move(*m_running);m_running.reset();auto future=m_completion.future();auto result=future.takeResult();
    if(!m_active||job.epoch!=m_epoch){launch();return;}
    if(!job.project||job.project!=m_project||job.canvas!=job.project->currentCanvas()||job.canvas->revision()!=job.revision
        ||job.canvas->filePath()!=job.currentPath||job.canvas->liveStrokeActive()||!contextMatches(job)) {
        stop(tr("The document or selected drive changed. Open the file again."),true);return;
    }
    if(!result.ok()){stop(result.cancelled?QString{}:QString::fromStdString(result.error),!result.cancelled);return;}
    m_active=false;m_adopting=true;m_adoptionCancelled=false;
    const auto epoch=m_epoch;const QPointer<EditorDocumentOpenRequest> guard(this);
    const bool adopted=job.project->adoptDocumentOpen(std::move(result));
    if(!guard)return;
    m_adopting=false;m_adoptionCancelled=false;
    if(m_epoch!=epoch){emit changed();return;}
    if(!adopted){m_error=job.project?job.project->error():tr("The document project is unavailable.");emit failed(m_error);}
    else emit ready(job.source);
    if(guard&&m_epoch==epoch&&!m_active)emit changed();
}
