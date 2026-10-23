#include "EditorFrameExporter.h"
#include "EditorToolUtils.h"
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <iiSocietyHelper/MediaExport.h>
#include <File.h>
#include <QImageWriter>
#include <QPointer>
#include <QtConcurrentRun>
#include <cmath>
#include <exception>
#include <limits>

namespace {
bool validDensity(double value) {
    return std::isfinite(value)&&value>0&&value/0.0254<=std::numeric_limits<int>::max()-0.5;
}
}

EditorFrameExporter::EditorFrameExporter(QObject *parent):EditorFrameExporter(exportSnapshot,parent) {}
EditorFrameExporter::EditorFrameExporter(ExportOperation operation,QObject *parent):QObject(parent),m_operation(std::move(operation)) {
    connect(&m_completion,&QFutureWatcher<QString>::finished,this,[this] {
        const auto error=m_completion.result();const auto destination=m_destination;
        const auto request=m_request;
        m_error=error;m_busy=false;
        const auto owner=QPointer<EditorFrameExporter>(this);
        if(error.isEmpty())emit saved(destination);else emit failed(destination,error);
        if(owner&&request==m_request&&!m_busy)emit stateChanged();
    });
}
EditorFrameExporter::~EditorFrameExporter(){disconnect(&m_completion,nullptr,this,nullptr);}
bool EditorFrameExporter::reject(const QString &message) {
    m_rejection=message;emit rejected(message);return false;
}
bool EditorFrameExporter::prepare(EditorFrameSnapshot snapshot,double pixelsPerInch) {
    if(m_busy)return reject(tr("An image export is already running."));
    if(!snapshot.document)return reject(tr("Open a canvas before exporting a frame."));
    if(!validDensity(pixelsPerInch))
        return reject(tr("Choose a finite positive export pixel density."));
    m_prepared=Request{std::move(snapshot),{},{},pixelsPerInch};m_error.clear();m_rejection.clear();
    emit stateChanged();return true;
}
void EditorFrameExporter::cancelPrepared() {
    if(!m_prepared)return;
    m_prepared.reset();emit stateChanged();
}
bool EditorFrameExporter::start(const QUrl &destination,const QString &format) {
    if(m_busy)return reject(tr("An image export is already running."));
    if(!m_operation)return reject(tr("Frame export is unavailable."));
    if(!m_prepared)return reject(tr("Choose a frame to export first."));
    if(!destination.isLocalFile()||destination.toLocalFile().isEmpty()||format.trimmed().isEmpty())
        return reject(tr("Choose a local export destination and format."));
    auto request=std::move(*m_prepared);m_prepared.reset();
    request.destination=destination;request.format=format.trimmed().toUpper();
    m_destination=destination;m_error.clear();m_rejection.clear();m_busy=true;
    ++m_request;
    // The operation and immutable snapshot are the entire worker ownership set.
    // Schedule before notifying observers, which may delete this owner at once.
    m_completion.setFuture(QtConcurrent::run([operation=m_operation,request=std::move(request)] {
        try{return operation(request);}
        catch(const std::exception &error){return tr("Could not export the frame: %1").arg(QString::fromUtf8(error.what()));}
        catch(...){return tr("Could not export the frame.");}
    }));
    emit stateChanged();return true;
}
QString EditorFrameExporter::exportSnapshot(const Request &request) {
    using namespace iiSharedCanvas;
    if(!request.snapshot.document)return tr("The chosen canvas frame is unavailable.");
    if(!validDensity(request.pixelsPerInch))return tr("Choose a finite positive export pixel density.");
    const auto checked=iiSocietyHelper::mediaExportDestination(request.destination,request.format=="IISC");
    if(!checked.error.isEmpty())return checked.error;
    const auto &snapshot=request.snapshot;
    auto rendered=snapshot.artboardId.empty()?renderFrame(*snapshot.document,snapshot.frame)
        :renderArtboard(*snapshot.document,snapshot.frame,snapshot.artboardId);
    if(!rendered.ok())return QString::fromStdString(rendered.message);
    if(request.format=="IISC") {
        Document flat;flat.extent={rendered.pixels.width,rendered.pixels.height};
        flat.assets.emplace_back(RasterAsset{"export.frame",std::move(rendered.pixels)});
        flat.layers.emplace_back(StaticBitmapLayer{{"export.layer","Exported frame"},StaticSource{"export.frame"}});
        const auto result=iiSocietyHelper::exportCanvasCopy(flat,QUrl::fromLocalFile(checked.path));
        return result.success?QString{}:result.error;
    }
    auto image=editorTools::image(rendered.pixels);
    image.setDotsPerMeterX(qRound(request.pixelsPerInch/0.0254));image.setDotsPerMeterY(image.dotsPerMeterX());
    // Encode directly into the provider's atomic stream. A failed codec leaves
    // the destination intact and never builds a second full encoded byte buffer.
    iiFileProvider::File::writeWith(checked.path,[&](QIODevice &device) {
        QImageWriter writer(&device,request.format.toLower().toLatin1());writer.setQuality(100);
        if(!writer.write(image))throw std::runtime_error(writer.errorString().toStdString());
    });
    return {};
}
