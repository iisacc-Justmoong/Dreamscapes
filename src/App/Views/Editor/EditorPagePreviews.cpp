#include "EditorPagePreviews.h"
#include "EditorToolUtils.h"
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <QtConcurrentRun>
#include <algorithm>
#include <exception>

EditorPagePreviews::EditorPagePreviews(Operation operation,QObject *parent)
    :QObject(parent),m_operation(std::move(operation)) {
    connect(&m_completion,&QFutureWatcher<QUrl>::finished,this,&EditorPagePreviews::complete);
}
EditorPagePreviews::~EditorPagePreviews() {
    disconnect(&m_completion,nullptr,this,nullptr);
    // The worker owns only its operation and immutable request, never this owner.
    // Discarding a view must not join an optional renderer on the input thread.
}
void EditorPagePreviews::cancelPending() { ++m_epoch;m_pending.clear();m_latest.clear(); }
void EditorPagePreviews::invalidate() { cancelPending();m_cache.clear(); }
QUrl EditorPagePreviews::source(const QString &key,Request request) {
    if(key.isEmpty()||!request.document||!m_operation)return {};
    const auto slot=QString::number(request.page)+':'+QString::number(request.output.width)+'x'+QString::number(request.output.height);
    if(const auto *cached=m_cache.object(key)) {
        // Returning to a cached frame supersedes any queued frame in this slot.
        m_pending.remove_if([&](const Job &job){return job.slot==slot;});
        m_latest.remove(slot);return *cached;
    }
    if(m_latest.value(slot)==key)return {};
    auto pending=std::find_if(m_pending.begin(),m_pending.end(),[&](const Job &job){return job.slot==slot;});
    Job job{slot,key,std::move(request),m_epoch};
    if(pending!=m_pending.end())*pending=std::move(job); // Keep FIFO position.
    else {
        if(m_pending.size()==128){m_latest.remove(m_pending.front().slot);m_pending.pop_front();}
        m_pending.push_back(std::move(job));
    }
    m_latest[slot]=key;launchPending();return {};
}
void EditorPagePreviews::launchPending() {
    if(m_running||m_pending.empty())return;
    m_running=std::move(m_pending.front());m_pending.pop_front();
    m_completion.setFuture(QtConcurrent::run([operation=m_operation,request=m_running->request] {
        try{return operation(request);}catch(const std::exception &){return QUrl{};}catch(...){return QUrl{};}
    }));
}
void EditorPagePreviews::complete() {
    const auto job=std::move(*m_running);m_running.reset();
    const auto result=m_completion.result();
    const bool current=job.epoch==m_epoch&&m_latest.value(job.slot)==job.key;
    if(current) {
        const auto cost=int(std::max<qsizetype>(128,result.toString().size()*sizeof(QChar)));
        m_cache.insert(job.key,new QUrl(result),cost);m_latest.remove(job.slot);
    }
    launchPending();
    // Publish last: a direct observer may replace or delete the owning project.
    if(current)emit ready(job.request.page);
}

QUrl EditorPagePreviews::render(const Request &request) {
    using namespace iiSharedCanvas;
    if (!request.document) return {};
    const auto *source=request.document.get();
    std::optional<Document> restored;
    if(const auto &visibility=request.visibility) {
        if(visibility->artboards.size()!=source->artboards.size()||visibility->layers.size()!=source->layers.size())return {};
        bool focused=false;
        for(std::size_t i=0;i<source->artboards.size();++i)focused|=source->artboards[i].visible!=visibility->artboards[i];
        for(std::size_t i=0;i<source->layers.size();++i)focused|=layerProperties(source->layers[i]).visible!=visibility->layers[i];
        if(focused) {
            // The SDK's immutable canvas snapshot includes all content, with
            // inactive pages hidden. Restore author visibility on this worker;
            // never clone its raster payload again on the GUI owner.
            restored.emplace(*source);
            for(std::size_t i=0;i<source->artboards.size();++i)restored->artboards[i].visible=visibility->artboards[i];
            for(std::size_t i=0;i<source->layers.size();++i)layerProperties(restored->layers[i]).visible=visibility->layers[i];
            source=&*restored;
        }
    }
    const auto &document = *source;
    const auto rendered = request.artboardId.empty()
        ? renderFrameRegion(document, request.frame, documentViewRegion(document), request.output, RasterSampling::Smooth)
        : renderArtboard(document, request.frame, request.artboardId, request.output, RasterSampling::Smooth);
    return rendered.ok() ? editorTools::dataImage(editorTools::image(rendered.pixels)) : QUrl{};
}
