#include "PhotoLibraryExporter.h"
#include "PhotoLibraryNative.h"
#include <iiSocietyHelper/MediaExport.h>
#include <iiFileProviderNative.h>
#include <File.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QPointer>
#include <QtConcurrentRun>
#include <atomic>
#include <exception>
#include <filesystem>
#include <mutex>

namespace {
struct PhotoRequest {
    std::atomic_bool replied=false;
    std::mutex mutex;
    std::shared_ptr<void> lifetime;
};
}

PhotoLibraryExporter::PhotoLibraryExporter(QObject *parent)
    : PhotoLibraryExporter(prepareImage, nativePhotoLibrarySaveOperation(), parent) {}

PhotoLibraryExporter::PhotoLibraryExporter(SaveOperation operation, QObject *parent)
    : PhotoLibraryExporter(prepareImage, std::move(operation), parent) {}

PhotoLibraryExporter::PhotoLibraryExporter(PrepareOperation prepare, SaveOperation operation, QObject *parent)
    : QObject(parent), m_prepare(std::move(prepare)), m_operation(std::move(operation)) {}

PhotoLibraryExporter::Preparation PhotoLibraryExporter::prepareImage(const QUrl &source)
{
    const auto path=source.toLocalFile();
    if(QFileInfo(path).suffix().compare("iisc",Qt::CaseInsensitive)==0) {
        auto temporary=std::make_shared<iiFileProvider::native::TemporaryDirectory>(std::filesystem::temp_directory_path());
        auto name=QFileInfo(path).completeBaseName();if(name.isEmpty())name=QStringLiteral("Dreamscapes");
        const auto prepared=iiFileProvider::File::pathString(temporary->path()/std::filesystem::u8path((name+".png").toStdString()));
        const auto result=iiSocietyHelper::exportMedia(source,QUrl::fromLocalFile(prepared));
        if(!result.success)return {{},result.error,{}};
        return {{prepared,QStringLiteral("image/png")},{},std::move(temporary)};
    }
    auto input=iiFileProvider::File::openRead(path);
    QImageReader reader(input.get());
    if(!reader.canRead()||!input->seek(0))return {{},tr("The original image could not be read."),{}};
    const auto mime=QMimeDatabase().mimeTypeForData(input.get()).name();
    if(!mime.startsWith("image/"))return {{},tr("The original image type could not be identified."),{}};
    return {{path,mime},{},{}};
}

bool PhotoLibraryExporter::save(const QUrl &source)
{
    if (m_busy) return reject(tr("A photo library save is already running."));
    if (!supported()) return reject(tr("Saving to Photos is unavailable on this device."));
    if (!source.isLocalFile() || source.toLocalFile().isEmpty())
        return reject(tr("The original image could not be read."));
    const auto operation=m_operation;
    const auto prepare=m_prepare;
    if (++m_request == 0) ++m_request;
    const auto request=m_request;
    const auto context=std::make_shared<PhotoRequest>();
    Completion completion=[self=QPointer<PhotoLibraryExporter>(this),request,context](QString identifier,QString error) {
        if(context->replied.exchange(true))return;
        { std::lock_guard guard(context->mutex);context->lifetime.reset(); }
        QMetaObject::invokeMethod(QCoreApplication::instance(),[self,request,
            identifier=std::move(identifier),error=std::move(error)]() mutable {
            if(self)self->complete(request,std::move(identifier),std::move(error));
        },Qt::QueuedConnection);
    };
    m_busy=true;m_rejection.clear();
    try {
        // Schedule before notifying a view that may delete this owner. The
        // accepted command is value-owned; only feedback uses the weak owner.
        (void)QtConcurrent::run([prepare,operation,source,context,completion] {
            Preparation prepared;
            try { prepared=prepare(source); }
            catch(const std::exception &error) {
                completion({},tr("Could not prepare the image: %1").arg(QString::fromUtf8(error.what())));return;
            } catch(...) { completion({},tr("Could not prepare the image."));return; }
            if(!prepared.error.isEmpty()||prepared.image.path.isEmpty()||prepared.image.mimeType.isEmpty()) {
                completion({},prepared.error.isEmpty()?tr("The original image could not be read."):prepared.error);return;
            }
            QMetaObject::invokeMethod(QCoreApplication::instance(),[operation,context,completion,prepared=std::move(prepared)] {
                { std::lock_guard guard(context->mutex);context->lifetime=prepared.lifetime; }
                // Keep the local prepared lease through dispatch as well. A
                // synchronous reply can reenter Qt and destroy/reuse the view.
                try { operation(prepared.image,completion); }
                catch(const std::exception &error) { completion({},tr("Could not save to Photos: %1").arg(QString::fromUtf8(error.what()))); }
                catch(...) { completion({},tr("Could not save to Photos.")); }
            },Qt::QueuedConnection);
        });
    } catch(const std::exception &error) { completion({},tr("Could not prepare the image: %1").arg(QString::fromUtf8(error.what()))); }
    catch(...) { completion({},tr("Could not prepare the image.")); }
    emit busyChanged();
    return true;
}

bool PhotoLibraryExporter::reject(const QString &message)
{
    m_rejection = message;
    emit rejected(message);
    return false;
}

void PhotoLibraryExporter::complete(qulonglong request, QString identifier, QString error)
{
    if (!m_busy || request != m_request) return;
    if (error.isEmpty() && identifier.isEmpty()) error = tr("Could not save to Photos.");
    m_busy = false;
    const auto owner = QPointer<PhotoLibraryExporter>(this);
    if (error.isEmpty()) emit saved(identifier);
    else emit failed(error);
    if (owner && request == m_request && !m_busy) emit busyChanged();
}
