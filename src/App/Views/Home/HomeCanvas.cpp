#include <iiFileProvider>
#include "HomeCanvas.h"
#include "App/Generation/ImageParameterCodec.h"
#include "App/Generation/ImageCanvas.h"
#include <Generation/ImageParameters.hpp>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPointer>
#include <QStandardPaths>
#include <QUuid>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
QImage rasterImage(const RasterLayer &pixels) {
    return QImage(reinterpret_cast<const uchar *>(pixels.pixels.data()),pixels.width,pixels.height,
                  pixels.width*4,QImage::Format_ARGB32).copy();
}
RasterLayer rasterPixels(const QImage &image) {
    const auto argb=image.convertToFormat(QImage::Format_ARGB32);
    auto pixels=makeRasterLayer(argb.width(),argb.height());
    for(int y=0;y<argb.height();++y)
        std::copy_n(reinterpret_cast<const quint32 *>(argb.constScanLine(y)),argb.width(),
                    pixels.pixels.begin()+y*argb.width());
    return pixels;
}
QString snapshotTemplate() {
    auto base=qEnvironmentVariable("DREAMSCAPES_TEMP_DIRECTORY");
    if(base.isEmpty()) base=QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    iiFileProvider::Directory().mkpath(base);
    return base+"/home-canvas-inputs-XXXXXX";
}
}
HomeCanvas::HomeCanvas(QQuickItem *parent):CanvasItem(parent),m_snapshots(snapshotTemplate()) {
    createRasterDocument(1024,1024);
    setBrushColor(QColor("#496c68")); setBrushSize(24); setBrushOpacity(1);
    setBrushOpacityEnabled(true); setToolMode("brush");
    connect(this,&CanvasItem::revisionChanged,this,&HomeCanvas::contentChanged);
    connect(this,&CanvasItem::documentChanged,this,&HomeCanvas::contentChanged);
}
HomeCanvas::~HomeCanvas() = default;

void HomeCanvas::wheelEvent(QWheelEvent *event) {
    // The home document fits its layout width; wheel input scrolls its host body.
    event->ignore();
}
bool HomeCanvas::fail(const QString &message) {
    const bool success = message.isEmpty();
    if(m_error!=message){m_error=message;emit inputErrorChanged();}
    return success;
}
bool HomeCanvas::hasContent() const {
    const auto *pixels=selectedRasterPixels();
    return pixels&&std::any_of(pixels->pixels.begin(),pixels->pixels.end(),[](auto p){return p>>24;});
}
bool HomeCanvas::addAttachment(const QUrl &source) {
    const auto previous=m_references.attachments().size();
    if(!m_references.add(source)) return fail(m_references.error());
    const QPointer<HomeCanvas> guard(this);
    if(previous!=m_references.attachments().size()) emit attachmentsChanged();
    return guard ? fail({}) : true;
}
bool HomeCanvas::removeAttachment(int index) {
    if(!m_references.remove(index)) return fail(m_references.error());
    const QPointer<HomeCanvas> guard(this); emit attachmentsChanged(); return guard ? fail({}) : true;
}
bool HomeCanvas::setAspectRatio(const QString &ratio) {
    const auto size=dreamscapes::homeReferenceOutputSize(ratio);
    if(size.isEmpty()) return fail(tr("Unsupported canvas ratio."));
    if(size==QSize(canvasWidth(),canvasHeight())) return fail({});
    if(liveStrokeActive()) cancelStroke();
    const auto *pixels=selectedRasterPixels();
    const auto prior=pixels?rasterImage(*pixels):QImage();
    QImage resized(size,QImage::Format_ARGB32); resized.fill(Qt::transparent);
    if(!prior.isNull()) {
        const auto fit=prior.size().scaled(size,Qt::KeepAspectRatio);
        QPainter painter(&resized); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRect(QPoint((size.width()-fit.width())/2,(size.height()-fit.height())/2),fit),prior);
    }
    const bool containsPixels = hasContent();
    if (!createRasterDocument(size.width(), size.height())
        || (containsPixels && !replaceSelectedPixels(rasterPixels(resized))))
        return fail(lastError());
    fitToView(); return fail({});
}
bool HomeCanvas::pasteAttachment(const QUrl &source,qreal itemX,qreal itemY) {
    if(!std::isfinite(itemX)||!std::isfinite(itemY)||liveStrokeActive())
        return fail(tr("Finish the current stroke before placing an image."));
    const QFileInfo info(source.toLocalFile());
    const auto canonical=QUrl::fromLocalFile(info.exists() ? info.canonicalFilePath() : info.absoluteFilePath());
    const auto &attachments=m_references.attachments();
    const auto attached=std::find_if(attachments.begin(),attachments.end(),
        [&](const auto &v){return v.toMap().value("source").toUrl()==canonical;});
    if(!source.isLocalFile() || attached==attachments.end()) return fail(tr("Attach the image before placing it on the canvas."));
    const auto point=QPointF((itemX-panX())/zoom(),(itemY-panY())/zoom());
    if(point.x()<0||point.y()<0||point.x()>=canvasWidth()||point.y()>=canvasHeight())
        return fail(tr("Drop the image inside the canvas."));
    QImageReader reader(attached->toMap().value("previewSource").toUrl().toLocalFile()); reader.setAutoTransform(true);
    const auto size=reader.size();
    if(size.isEmpty()||qint64(size.width())*size.height()>64LL*1024*1024)
        return fail(tr("The attached image is no longer readable."));
    if(size.width()>2048||size.height()>2048)reader.setScaledSize(size.scaled(2048,2048,Qt::KeepAspectRatio));
    const auto image=reader.read();
    if(image.isNull())return fail(tr("Cannot read the attached image: %1").arg(reader.errorString()));
    auto dest=rasterImage(*selectedRasterPixels());
    const auto fit=image.size().scaled(QSize(canvasWidth(),canvasHeight()),Qt::KeepAspectRatio);
    // Keep small images at native size; only shrink oversized inputs.
    const auto placed=(image.width()>fit.width()||image.height()>fit.height())?fit:image.size();
    const auto x=std::clamp(qRound(point.x()-placed.width()/2),0,canvasWidth()-placed.width());
    const auto y=std::clamp(qRound(point.y()-placed.height()/2),0,canvasHeight()-placed.height());
    {QPainter painter(&dest);painter.setRenderHint(QPainter::SmoothPixmapTransform);
     painter.drawImage(QRect(QPoint(x,y),placed),image);}
    if(!replaceSelectedPixels(rasterPixels(dest)))return fail(lastError());
    return fail({});
}
QVariantMap HomeCanvas::generationParameters(const QString &prompt,const QString &model,
                                              const QString &ratio,int count) {
    if(!setAspectRatio(ratio))return {};
    QVariantList references;
    if(hasContent()) {
        if(!m_snapshots.isValid()){fail(tr("Cannot create the canvas input directory."));return {};}
        const auto path=m_snapshots.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)+".png");
        QImage rendered(canvasWidth(), canvasHeight(), QImage::Format_RGB32);
        rendered.fill(QColor("#f3f1e8"));
        { QPainter painter(&rendered); painter.drawImage(0, 0, rasterImage(*selectedRasterPixels())); }
        iiFileProvider::SaveFile output(path);
        if(!output.open(QIODevice::WriteOnly)||!rendered.save(&output,"PNG")||!output.commit()) {
            fail(tr("Cannot export the canvas for generation."));return {};
        }
        references.append(QUrl::fromLocalFile(path).toString());
    }
    references.append(m_references.previewSources());
    auto args=dreamscapes::imageParametersToMap(iiLocalDiffusion::ImageParameters::defaults());
    args["prompt"]=prompt; args["model"]=model; args["width"]=canvasWidth(); args["height"]=canvasHeight();
    args["outputCount"]=count; args["referenceImages"]=references;
    fail({}); return args;
}
