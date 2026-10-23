#include "ImageOpenRequest.h"
#include <QDir>
#include <QSet>
#include <QTimer>

ImageOpenRequest::ImageOpenRequest(QObject *parent) : QObject(parent) {}
ImageOpenRequest::~ImageOpenRequest() {
    // Retire our intent before stopping the shared transport. Its notifications
    // may synchronously reach this receiver while destruction is in progress.
    const bool owned=ownsDownload(); m_active=false; ++m_epoch;
    if(owned) m_download->cancel();
}
void ImageOpenRequest::setContainer(iiSocietyContainer::Container *value) {
    if(m_container==value)return;
    QPointer<ImageOpenRequest> guard(this); cancel(); if(!guard)return;
    if(m_container)disconnect(m_container,nullptr,this,nullptr);
    m_container=value;
    if(value) {
        connect(value,&iiSocietyContainer::Container::stateChanged,this,[this] {
            if(m_active&&!sameStorage())stop(true,tr("The selected drive changed. Select the images again."));
            else if(!m_active&&!m_retryEntries.isEmpty()&&!sameStorage()){m_retryEntries.clear();emit changed();}
        });
        connect(value,&QObject::destroyed,this,[this] { if(m_active)stop(true,tr("The selected drive is unavailable.")); });
    }
    emit bindingsChanged();
}
void ImageOpenRequest::setDownload(iiSocietyHelper::FileDownload *value) {
    if(m_download==value)return;
    QPointer<ImageOpenRequest> guard(this); cancel(); if(!guard)return;
    if(m_download)disconnect(m_download,nullptr,this,nullptr);
    m_download=value;
    if(value) {
        connect(value,&iiSocietyHelper::FileDownload::changed,this,&ImageOpenRequest::downloadChanged);
        connect(value,&iiSocietyHelper::FileDownload::ready,this,&ImageOpenRequest::downloaded);
        connect(value,&QObject::destroyed,this,[this] { if(m_active)stop(true,tr("The image download is unavailable.")); });
    }
    emit bindingsChanged();
}
QUrl ImageOpenRequest::originalSource(const QVariantMap &entry) {
    // previewSource/thumbnailSource are presentation only. Native generated
    // images carry documentSource, even when imageSource is a provider URL.
    for(const auto *field:{"documentSource","source"}) {
        const auto source=entry.value(field).toUrl();
        if(!source.isEmpty())return source;
    }
    const auto path=entry.value("path").toString();
    if(!path.isEmpty())return QDir::isAbsolutePath(path)?QUrl::fromLocalFile(path):QUrl{};
    for(const auto *field:{"imageSource","mediaSource"}) {
        const auto source=entry.value(field).toUrl();
        if(!source.isEmpty())return source;
    }
    return {};
}
bool ImageOpenRequest::sameStorage() const {
    return m_id.isEmpty() || (m_container&&m_container->available()
        &&m_container->containerId()==m_id&&m_container->rootPath()==m_root);
}
bool ImageOpenRequest::ownsDownload() const {
    return m_active&&!m_pending.isEmpty()&&m_download&&m_download->active()
        &&m_download->state().value("key")==m_pending&&m_download->state().value("containerId")==m_id;
}
bool ImageOpenRequest::reject(const QString &message) { emit rejected(message); return false; }
bool ImageOpenRequest::start(const QVariantList &entries) {
    if(entries.isEmpty()||entries.size()>1000)return reject(tr("Select between 1 and 1000 images to open."));
    const auto capturedId=m_container?m_container->containerId():QString{};
    const auto capturedRoot=m_container?m_container->rootPath():QString{};
    std::vector<Entry> selected; QSet<QString> identities; bool bound=false;
    for(const auto &value:entries) {
        const auto entry=value.toMap(); const auto type=entry.value("mediaType").toString();
        if(entry.isEmpty()||type=="Video"||type=="Canvas"
            ||(entry.contains("isImage")&&!entry.value("isImage").toBool()&&entry.value("documentSource").toUrl().isEmpty())
            ||(entry.contains("exists")&&!entry.value("exists").toBool()))
            return reject(tr("Select available image records to open in the editor."));
        const auto nested=entry.value("alias").toMap();
        Entry next; next.selected=entry; next.key=entry.value("key",nested.value("key")).toString();
        const auto id=entry.value("containerId",nested.value("containerId")).toString(); QString identity;
        if(!next.key.isEmpty()||!id.isEmpty()) {
            if(next.key.isEmpty()||id.isEmpty()||!m_container||!m_container->available()||id!=m_container->containerId())
                return reject(tr("The images' drive information is missing or changed. Select them again."));
            const auto alias=m_container->alias(next.key);
            if(!alias.value("exists").toBool()||alias.value("containerId")!=id)
                return reject(tr("A selected image no longer exists. Refresh files and try again."));
            if(!alias.value("isImage").toBool()&&entry.value("documentSource").toUrl().isEmpty())
                return reject(tr("Select images to open in the editor."));
            if(!alias.value("available").toBool()&&!m_download)
                return reject(tr("The image download is unavailable."));
            identity=id+'\n'+next.key; bound=true;
        } else {
            if(entry.contains("available")&&!entry.value("available").toBool())
                return reject(tr("The image's drive information is missing. Select it again."));
            next.source=originalSource(entry);
            if(!next.source.isLocalFile()||next.source.toLocalFile().isEmpty())
                return reject(tr("The original image is unavailable. Select the original file again."));
            identity=next.source.toString(QUrl::FullyEncoded);
        }
        if(!identities.contains(identity)){identities.insert(identity); selected.push_back(std::move(next));}
    }
    const auto before=m_epoch;const bool retired=m_active||!m_retryEntries.isEmpty();
    QPointer<ImageOpenRequest> guard(this); cancel(); if(!guard)return true;
    // A cancellation observer may accept a newer request. It owns the intent.
    if(m_epoch!=before+(retired?1:0))return true;
    // A newly accepted explicit open supersedes the shared single-file intent.
    if(m_download&&m_download->active()) {
        const auto epoch=m_epoch;m_download->cancel();if(!guard||m_epoch!=epoch)return true;
    }
    ++m_epoch; m_entries=std::move(selected); m_completed=0; m_total=int(m_entries.size());
    m_retryEntries=entries;
    m_id=bound?capturedId:QString{};m_root=bound?capturedRoot:QString{};
    m_pending.clear();m_error.clear();m_active=true;
    schedule(); emit changed(); return true;
}
void ImageOpenRequest::stop(bool failure,const QString &message) {
    if(!m_active)return;
    const bool owned=ownsDownload(); m_active=false; const auto epoch=++m_epoch;
    m_entries.clear();m_pending.clear();m_error=message;
    QPointer<ImageOpenRequest> guard(this);
    if(owned)m_download->cancel(); if(!guard||m_epoch!=epoch)return;
    if(failure)emit failed(message);else emit cancelled();
    if(guard&&m_epoch==epoch&&!m_active)emit changed();
}
void ImageOpenRequest::cancel() {
    const bool retained=!m_retryEntries.isEmpty();m_retryEntries.clear();
    if(m_active)stop(false);
    else if(retained){++m_epoch;emit changed();}
}
bool ImageOpenRequest::retry() {
    if(!canRetry())return reject(tr("Select the images again before retrying."));
    const auto entries=m_retryEntries;return start(entries);
}
void ImageOpenRequest::schedule() {
    const auto epoch=m_epoch;
    QTimer::singleShot(0,this,[this,epoch]{if(m_active&&m_epoch==epoch)advance();});
}
void ImageOpenRequest::prepare(Entry &entry,const QVariantMap &alias) {
    entry.prepared=entry.selected;
    if(!alias.isEmpty()) {
        // Native Alias records may change availability. Generated result records
        // keep their generation schema and receive command URLs separately.
        if(entry.selected.contains("key")&&entry.selected.contains("containerId"))
            for(const auto *field:{"key","containerId","section","name","path","source","available","exists","version"})
                entry.prepared.insert(field,alias.value(field));
        entry.source=alias.value("source").toUrl();entry.version=alias.value("version").toString();
        if(entry.selected.contains("alias"))entry.prepared["alias"]=alias;
        if(!entry.selected.value("documentSource").toUrl().isEmpty()) {
            entry.prepared["documentSource"]=entry.source;
        }
    }
}
void ImageOpenRequest::advance() {
    if(!sameStorage()){stop(true,tr("The selected drive changed. Select the images again."));return;}
    while(m_completed<m_total) {
        auto &entry=m_entries[std::size_t(m_completed)];
        if(entry.key.isEmpty()){prepare(entry,{});++m_completed;continue;}
        const auto alias=m_container->alias(entry.key);
        if(!alias.value("exists").toBool()||alias.value("containerId")!=m_id) {
            stop(true,tr("A selected image no longer exists. Refresh files and try again."));return;
        }
        if(!alias.value("available").toBool()) {
            if(!m_download){stop(true,tr("The image download is unavailable."));return;}
            m_pending=entry.key;const auto epoch=m_epoch;QPointer<ImageOpenRequest> guard(this);
            // Notifications may cancel/delete this view before start returns.
            const auto key=m_pending,id=m_id;
            const bool accepted=m_download->start(key,id);
            if(guard&&m_active&&m_epoch==epoch&&!accepted)stop(true,tr("The image download could not be started. Refresh files and try again."));
            return;
        }
        if(!alias.value("source").toUrl().isLocalFile()) {
            stop(true,tr("The original image is unavailable on this device."));return;
        }
        prepare(entry,alias);++m_completed;
    }
    QVariantList result,sources;
    for(const auto &entry:m_entries) {
        if(!entry.key.isEmpty()) {
            const auto current=m_container->alias(entry.key);
            if(!current.value("exists").toBool()||!current.value("available").toBool()
                ||current.value("source").toUrl()!=entry.source||current.value("version").toString()!=entry.version) {
                stop(true,tr("A selected image changed while preparing. Select the images again."));return;
            }
        }
        result.append(entry.prepared);
        sources.append(entry.source);
    }
    m_active=false;const auto epoch=++m_epoch;m_entries.clear();m_pending.clear();m_retryEntries.clear();
    QPointer<ImageOpenRequest> guard(this);emit ready(result,sources);
    if(guard&&m_epoch==epoch&&!m_active)emit changed();
}
void ImageOpenRequest::downloadChanged() {
    if(!m_active||m_pending.isEmpty()||!m_download)return;
    const auto state=m_download->state();
    if(state.value("key")!=m_pending||state.value("containerId")!=m_id){stop(false);return;}
    const auto phase=state.value("state").toString();
    if(phase=="failed")stop(true,tr("The original image download failed (%1). Retry or refresh files.").arg(state.value("error").toString()));
    else if(phase=="cancelled")stop(false);
}
void ImageOpenRequest::downloaded(const QVariantMap &file) {
    if(!m_active||m_pending.isEmpty()||file.value("key")!=m_pending||file.value("containerId")!=m_id)return;
    if(!sameStorage()){stop(true,tr("The selected drive changed. Select the images again."));return;}
    const auto fresh=m_container->alias(m_pending);
    if(!fresh.value("exists").toBool()||!fresh.value("available").toBool()||!fresh.value("source").toUrl().isLocalFile()
        ||fresh.value("source").toUrl()!=file.value("source").toUrl()) {
        stop(true,tr("The downloaded original is unavailable. Refresh files and try again."));return;
    }
    prepare(m_entries[std::size_t(m_completed)],fresh);++m_completed;m_pending.clear();
    schedule();emit changed();
}
