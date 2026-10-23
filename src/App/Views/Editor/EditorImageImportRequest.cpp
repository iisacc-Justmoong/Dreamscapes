#include "EditorImageImportRequest.h"
#include "EditorProject.h"
#include <QtConcurrentRun>
#include <exception>

EditorImageImportRequest::EditorImageImportRequest(QObject *parent)
    :EditorImageImportRequest(dreamscapes::prepareImageImport,parent) {}
EditorImageImportRequest::EditorImageImportRequest(dreamscapes::ImageImportOperation operation,QObject *parent)
    :QObject(parent),m_operation(std::move(operation)) {
    connect(&m_completion,&QFutureWatcher<dreamscapes::ImageImportResult>::finished,this,&EditorImageImportRequest::complete);
}
EditorImageImportRequest::~EditorImageImportRequest() {
    m_active=false;++m_epoch;
    if(m_running)m_running->request.cancelled->store(true);
    if(m_pending)m_pending->request.cancelled->store(true);
    disconnect(&m_completion,nullptr,this,nullptr);
    // No join: the operation owns values and cancellation, never a GUI object.
}
EditorProject *EditorImageImportRequest::project() const {return m_project;}
void EditorImageImportRequest::setProject(EditorProject *value) {
    if(m_project==value)return;
    const auto binding=++m_projectBindingEpoch;
    if(m_project)disconnect(m_project,nullptr,this,nullptr);
    m_project=value;bindCanvas();
    if(value) {
        connect(value,&EditorProject::currentCanvasChanged,this,[this,binding] {
            if(m_projectBindingEpoch!=binding)return;
            // A retry in the terminal observer already owns the current canvas.
            bindCanvas();
            if(m_active)stop(tr("The canvas changed while preparing images. Select them again."),true);
        });
        connect(value,&QObject::destroyed,this,[this,binding] {
            if(m_projectBindingEpoch!=binding)return;
            const auto retired=++m_projectBindingEpoch;bindCanvas();
            const QPointer<EditorImageImportRequest> guard(this);
            if(m_active)stop(tr("The image project is unavailable."),true);
            if(guard&&m_projectBindingEpoch==retired)emit bindingsChanged();
        });
    }
    // Publish the target before cancellation callbacks. A callback may replace
    // this binding, start a newer command, or destroy either target or owner.
    const QPointer<EditorImageImportRequest> guard(this);cancel();
    if(guard&&m_projectBindingEpoch==binding)emit bindingsChanged();
}
void EditorImageImportRequest::bindCanvas() {
    if(m_canvas)disconnect(m_canvas,nullptr,this,nullptr);
    m_canvas=m_project?m_project->currentCanvas():nullptr;
    if(!m_canvas)return;
    const auto changed=[this]{if(m_active)stop(tr("The canvas changed while preparing images. Select them again."),true);};
    connect(m_canvas,&EditorCanvas::revisionChanged,this,changed);
    connect(m_canvas,&EditorCanvas::documentChanged,this,changed);
    connect(m_canvas,&QObject::destroyed,this,changed);
}
void EditorImageImportRequest::setContainer(iiSocietyContainer::Container *value) {
    if(m_container==value)return;
    const auto binding=++m_containerBindingEpoch;
    if(m_container)disconnect(m_container,nullptr,this,nullptr);
    m_container=value;
    if(value) {
        connect(value,&iiSocietyContainer::Container::stateChanged,this,[this,binding] {
            if(m_containerBindingEpoch!=binding)return;
            if(const auto *job=current();job&&!sourceContextMatches(*job))stop(tr("The selected originals or drive changed. Select them again."),true);
        });
        connect(value,&QObject::destroyed,this,[this,binding] {
            if(m_containerBindingEpoch!=binding)return;
            const auto retired=++m_containerBindingEpoch;
            const QPointer<EditorImageImportRequest> guard(this);
            if(m_active)stop(tr("The selected drive is unavailable."),true);
            if(guard&&m_containerBindingEpoch==retired)emit bindingsChanged();
        });
    }
    const QPointer<EditorImageImportRequest> guard(this);cancel();
    if(guard&&m_containerBindingEpoch==binding)emit bindingsChanged();
}
const EditorImageImportRequest::Job *EditorImageImportRequest::current() const {
    if(!m_active)return nullptr;
    if(m_pending&&m_pending->epoch==m_epoch)return &*m_pending;
    return m_running&&m_running->epoch==m_epoch?&*m_running:nullptr;
}
bool EditorImageImportRequest::sourceContextMatches(const Job &job) const {
    if(job.container!=m_container)return false;
    if(job.containerId.isEmpty())return true;
    if(!m_container||!m_container->available()||m_container->containerId()!=job.containerId||m_container->rootPath()!=job.containerRoot)return false;
    for(const auto &value:job.aliases) {
        const auto captured=value.toMap(),now=m_container->alias(captured.value("key").toString());
        if(!now.value("exists").toBool()||!now.value("available").toBool()
            ||now.value("source")!=captured.value("source")||now.value("version")!=captured.value("version"))return false;
    }
    return true;
}
bool EditorImageImportRequest::start(const QVariantList &sources,const QVariantList &entries,bool attached) {
    const auto reject=[this](const QString &message){emit rejected(message);return false;};
    if(m_adopting)return reject(tr("The previous image command is being applied."));
    if(!m_project||!m_operation)return reject(tr("The image project is unavailable."));
    if(sources.isEmpty()||sources.size()>1000||(!entries.isEmpty()&&entries.size()!=sources.size()))
        return reject(tr("Select between 1 and 1000 original images with matching records."));
    Job job;job.project=m_project;job.canvas=m_project->currentCanvas();job.container=m_container;job.revision=job.canvas->revision();
    job.path=job.canvas->filePath();job.sources=sources;job.entries=entries;job.attached=attached&&job.canvas->documentReady();
    if(m_container) {
        if(!m_container->available())return reject(tr("The selected drive is unavailable."));
        job.containerId=m_container->containerId();job.containerRoot=m_container->rootPath();
        for(qsizetype i=0;i<entries.size();++i) {
            const auto entry=entries[i].toMap(),nested=entry.value("alias").toMap();
            const auto key=entry.value("key",nested.value("key")).toString(),id=entry.value("containerId",nested.value("containerId")).toString();
            if(key.isEmpty()&&id.isEmpty())continue;
            const auto alias=m_container->alias(key);
            const auto expectedVersion=(nested.isEmpty()?entry:nested).value("version").toString();
            if(key.isEmpty()||id!=job.containerId||!alias.value("exists").toBool()||!alias.value("available").toBool()
                ||alias.value("source").toUrl()!=sources[i].toUrl()||(!expectedVersion.isEmpty()&&alias.value("version").toString()!=expectedVersion))
                return reject(tr("The selected original changed. Select it again."));
            job.aliases.append(alias);
        }
    }
    try {job.request=m_project->imageImportRequest(sources,job.attached);}
    catch(const std::exception &error){return reject(QString::fromUtf8(error.what()));}
    job.request.storageRoot=std::filesystem::path(job.containerRoot.toStdU16String());
    job.request.cancelled=std::make_shared<std::atomic_bool>(false);
    const auto before=m_epoch;const bool retired=m_active||m_pending.has_value();
    const QPointer<EditorImageImportRequest> guard(this);cancel();
    if(!guard||m_epoch!=before+(retired?1:0))return true; // Reentrant newer intent owns the command.
    if(!job.project||job.project!=m_project||job.canvas!=m_project->currentCanvas()||job.canvas->revision()!=job.revision
        ||job.canvas->liveStrokeActive()||!sourceContextMatches(job))return reject(tr("The canvas or selected drive changed. Select them again."));
    job.epoch=++m_epoch;m_pending=std::move(job);m_active=true;m_error.clear();launch();emit changed();return true;
}
void EditorImageImportRequest::stop(const QString &message,bool failure) {
    const bool active=m_active;m_active=false;const auto epoch=++m_epoch;
    if(m_running)m_running->request.cancelled->store(true);
    if(m_pending)m_pending->request.cancelled->store(true);
    m_pending.reset();m_error=message;
    if(!active)return;
    const QPointer<EditorImageImportRequest> guard(this);
    if(failure)emit failed(message);else emit cancelled();
    if(guard&&m_epoch==epoch&&!m_active)emit changed();
}
void EditorImageImportRequest::cancel() {
    if(m_adopting) {
        if(!m_adoptionCancelled){m_adoptionCancelled=true;++m_epoch;emit cancelled();}
    } else if(m_active||m_pending)stop({},false);
}
void EditorImageImportRequest::launch() {
    if(m_running||!m_pending)return;
    m_running=std::move(m_pending);m_pending.reset();
    m_completion.setFuture(QtConcurrent::run([operation=m_operation,request=m_running->request] {
        try{return operation(request);}
        catch(const std::exception &error){dreamscapes::ImageImportResult r;r.error=error.what();return r;}
        catch(...){dreamscapes::ImageImportResult r;r.error="The originals could not be prepared.";return r;}
    }));
}
void EditorImageImportRequest::complete() {
    auto job=std::move(*m_running);m_running.reset();
    auto future=m_completion.future();auto result=future.takeResult();
    if(!m_active||job.epoch!=m_epoch){launch();return;}
    if(!job.project||job.project!=m_project||job.canvas!=job.project->currentCanvas()
        ||job.canvas->revision()!=job.revision||job.canvas->filePath()!=job.path||job.canvas->liveStrokeActive()
        ||!sourceContextMatches(job)) {
        stop(tr("The canvas, selected originals or drive changed. Select them again."),true);return;
    }
    if(!result.ok()){stop(result.cancelled?QString{}:QString::fromStdString(result.error),!result.cancelled);return;}
    if(result.firstNew<0||std::size_t(result.firstNew)>result.document.artboards.size()
        ||result.pageSources.empty()||result.pageSources.size()!=result.document.artboards.size()-std::size_t(result.firstNew)) {
        stop(tr("The imported artboard sources are invalid."),true);return;
    }
    QVariantList mappedEntries,mappedSources;
    for(const auto index:result.pageSources) {
        if(index>=std::size_t(job.sources.size())){stop(tr("The imported artboard sources are invalid."),true);return;}
        mappedSources.append(job.sources[qsizetype(index)]);
        mappedEntries.append(job.entries.isEmpty()?QVariantMap{}:job.entries[qsizetype(index)]);
    }
    m_active=false;m_adopting=true;m_adoptionCancelled=false;
    const auto epoch=m_epoch;const QPointer<EditorImageImportRequest> guard(this);
    const bool adopted=job.project->adoptImageImport(std::move(result),job.attached);
    if(!guard)return;
    m_adopting=false;m_adoptionCancelled=false;
    if(m_epoch!=epoch){emit changed();return;}
    if(!adopted){m_error=job.project?job.project->error():tr("The image project is unavailable.");emit failed(m_error);}
    else emit ready(mappedEntries,mappedSources,job.attached);
    if(guard&&m_epoch==epoch&&!m_active)emit changed();
}
