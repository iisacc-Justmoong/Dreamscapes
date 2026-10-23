#include "ImageFileExporter.h"

#include <iiSocietyHelper/MediaExport.h>
#include <QFileInfo>
#include <QPointer>
#include <QtConcurrentRun>
#include <exception>

ImageFileExporter::ImageFileExporter(QObject *parent)
    : ImageFileExporter([](const QUrl &source, const QUrl &destination, bool video) {
        const auto result = iiSocietyHelper::exportMedia(source, destination, video);
        return result.success ? QString{} : result.error;
    }, parent) {}

ImageFileExporter::ImageFileExporter(SaveOperation operation, QObject *parent)
    : QObject(parent), m_operation(std::move(operation))
{
    connect(&m_completion, &QFutureWatcher<QString>::finished, this, [this] {
        const auto error = m_completion.result();
        const auto destination = m_destination;
        const auto request = m_request;
        m_busy = false;
        const auto owner = QPointer<ImageFileExporter>(this);
        if (error.isEmpty()) emit saved(destination);
        else emit failed(error);
        if (owner && request == m_request && !m_busy) emit busyChanged();
    });
}

QString ImageFileExporter::suggestedFileName(const QUrl &source) const
{
    const QFileInfo file(source.toLocalFile());
    const auto name = file.suffix().compare("iisc", Qt::CaseInsensitive) == 0
        ? (file.completeBaseName().isEmpty() ? QStringLiteral("Dreamscapes") : file.completeBaseName()) + ".png" : file.fileName();
    return name.isEmpty() ? QStringLiteral("Dreamscapes.png") : name;
}

bool ImageFileExporter::save(const QUrl &source, const QUrl &destination)
{ return saveMedia(source,destination,false); }
bool ImageFileExporter::saveVideo(const QUrl &source, const QUrl &destination)
{ return saveMedia(source,destination,true); }
bool ImageFileExporter::saveMedia(const QUrl &source, const QUrl &destination, bool video) {
    if (m_busy) return reject(tr("A file export is already running."));
    bool localDestination = destination.isLocalFile();
#ifdef Q_OS_ANDROID
    localDestination = localDestination || destination.scheme() == "content";
#endif
    if (!source.isLocalFile() || source.toLocalFile().isEmpty() || !localDestination || !m_operation) {
        return reject(tr("Choose a local source and a location to save the file."));
    }
    m_destination = destination;
    ++m_request;
    m_rejection.clear();
    m_busy = true;
    // The task owns its inputs and operation, not the view. Accepted work can
    // finish after this QObject closes; only live receivers observe its result.
    m_completion.setFuture(QtConcurrent::run([operation = m_operation, source, destination, video] {
        try { return operation(source, destination, video); }
        catch (const std::exception &error) { return tr("Could not export the file: %1").arg(QString::fromUtf8(error.what())); }
        catch (...) { return tr("Could not export the file."); }
    }));
    // A busy observer may close the view immediately. Schedule first, and do not
    // access the owner after notifying it.
    emit busyChanged();
    return true;
}
bool ImageFileExporter::reject(const QString &message) {
    m_rejection=message;emit rejected(message);return false;
}
