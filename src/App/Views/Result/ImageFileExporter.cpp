#include "ImageFileExporter.h"

#include <iiSocietyHelper.h>
#include <QFileInfo>

QString ImageFileExporter::suggestedFileName(const QUrl &source) const
{
    const auto name = QFileInfo(source.toLocalFile()).fileName();
    return name.isEmpty() ? QStringLiteral("Dreamscapes.png") : name;
}

bool ImageFileExporter::save(const QUrl &source, const QUrl &destination)
{ return saveMedia(source,destination,false); }
bool ImageFileExporter::saveVideo(const QUrl &source, const QUrl &destination)
{ return saveMedia(source,destination,true); }
bool ImageFileExporter::saveMedia(const QUrl &source, const QUrl &destination, bool video) {
    iiSocietyHelper::FileSystem files;
    if (!files.exportMedia(source, destination, video)) { emit failed(files.errorString()); return false; }
    emit saved(destination); return true;
}
