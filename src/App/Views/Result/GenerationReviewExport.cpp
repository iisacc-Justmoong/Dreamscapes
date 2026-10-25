#include "GenerationReviewExport.h"
#include <iiSocietyHelper/MediaExport.h>
#include <iiFileProviderStorage.h>
#include <QDateTime>
#include <QFileInfo>
#include <QPointer>
#include <QPromise>
#include <QUuid>
#include <QtConcurrentRun>
#include <filesystem>
#include <stdexcept>

GenerationReviewExport::GenerationReviewExport(QObject* parent)
    : GenerationReviewExport([](const QUrl& source,const QUrl& destination,bool video) {
        const auto result=iiSocietyHelper::exportMedia(source,destination,video);
        return result.success ? QString{} : result.error;
    },parent) {}

GenerationReviewExport::GenerationReviewExport(ImageFileExporter::SaveOperation operation,QObject* parent)
    : QObject(parent),m_operation(std::move(operation)) {
    connect(&m_completion,&QFutureWatcher<Outcome>::progressValueChanged,this,[this](int value){m_processed=value;emit changed();});
    connect(&m_completion,&QFutureWatcher<Outcome>::finished,this,[this]{
        m_outcome=m_completion.result();m_busy=false;m_processed=m_total;
        const auto owner=QPointer<GenerationReviewExport>(this);const auto summary=result();
        emit changed();if(owner)emit completed(summary);
    });
}
QVariantMap GenerationReviewExport::result() const {
    return {{"total",m_total},{"succeeded",m_outcome.succeeded},{"failed",m_outcome.failed},
        {"directory",m_outcome.directory},{"errors",m_outcome.errors}};
}
bool GenerationReviewExport::start(const QVariantList& entries,const QUrl& folder) {
    if(m_busy || entries.isEmpty() || !folder.isLocalFile() || !m_operation) {
        emit rejected(tr("Choose results and a local export folder; wait for the current export to finish."));return false;
    }
    m_busy=true;m_total=entries.size();m_processed=0;m_outcome={};
    const auto leaf=QStringLiteral("Dreamscapes-review-%1-%2")
        .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss"),QUuid::createUuid().toString(QUuid::Id128));
    // Accepted exports own immutable inputs and can finish after their view closes.
    m_completion.setFuture(QtConcurrent::run([entries,folder,leaf,operation=m_operation](QPromise<Outcome>& promise){
        Outcome result;promise.setProgressRange(0,entries.size());
        try {
            const auto parent=std::filesystem::path(folder.toLocalFile().toStdU16String());
            if(!std::filesystem::is_directory(parent))throw std::runtime_error("The export folder does not exist.");
            const auto directory=parent/leaf.toStdU16String();
            if(!iiFileProvider::native::createDirectory(directory))throw std::runtime_error("The export directory already exists.");
            result.directory=QUrl::fromLocalFile(QString::fromStdU16String(directory.u16string()));
            for(qsizetype index=0;index<entries.size();++index) {
                const auto entry=entries[index].toMap();const bool video=entry.value("mediaType")=="Video";
                QUrl source;
                for (const auto* field : {"documentSource", "mediaSource", "imageSource"}) {
                    const QUrl candidate(entry.value(field).toString());
                    if (!candidate.isEmpty()) { source = candidate; break; }
                }
                auto suffix=video?QFileInfo(source.toLocalFile()).suffix():QStringLiteral("png");
                if(suffix.isEmpty())suffix=video?QStringLiteral("mp4"):QStringLiteral("png");
                const auto target=directory/QStringLiteral("%1.%2").arg(index+1,4,10,QLatin1Char('0')).arg(suffix).toStdU16String();
                QString error;
                try {
                    error=source.isLocalFile()?operation(source,QUrl::fromLocalFile(QString::fromStdU16String(target.u16string())),video)
                        : tr("The result has no local original to export.");
                } catch(const std::exception& failure){error=QString::fromUtf8(failure.what());}
                catch(...){error=tr("The result could not be exported.");}
                if(error.isEmpty())++result.succeeded;
                else {++result.failed;result.errors.append(tr("Result %1: %2").arg(index+1).arg(error));}
                promise.setProgressValue(index+1);
            }
        } catch(const std::exception& error) {
            result.failed=entries.size()-result.succeeded;result.errors.append(QString::fromUtf8(error.what()));
        } catch(...) { result.failed=entries.size()-result.succeeded;result.errors.append(tr("The export directory could not be prepared.")); }
        promise.addResult(result);
    }));
    emit changed();return true;
}
