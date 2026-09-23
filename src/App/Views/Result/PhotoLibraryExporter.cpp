#include "PhotoLibraryExporter.h"
#include "PhotoLibraryNative.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QPointer>

PhotoLibraryExporter::PhotoLibraryExporter(QObject *parent)
    : PhotoLibraryExporter(nativePhotoLibrarySaveOperation(), parent) {}

PhotoLibraryExporter::PhotoLibraryExporter(SaveOperation operation, QObject *parent)
    : QObject(parent), m_operation(std::move(operation)) {}

bool PhotoLibraryExporter::save(const QUrl &source)
{
    if (m_busy) return false;
    if (!supported()) {
        emit failed(tr("Saving to Photos is unavailable on this device."));
        return false;
    }
    const auto path = source.toLocalFile();
    if (!source.isLocalFile() || !QFileInfo(path).isFile() || !QImageReader(path).canRead()) {
        emit failed(tr("The original image could not be read."));
        return false;
    }
    m_busy = true;
    emit busyChanged();
    m_operation(path, [self = QPointer<PhotoLibraryExporter>(this)](QString identifier, QString error) {
        // Native permission/import callbacks can arrive on any thread, after the view closes.
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, identifier, error] {
            if (!self) return;
            self->m_busy = false;
            emit self->busyChanged();
            if (!error.isEmpty()) emit self->failed(error);
            else if (identifier.isEmpty()) emit self->failed(tr("Could not save to Photos."));
            else emit self->saved(identifier);
        }, Qt::QueuedConnection);
    });
    return true;
}
