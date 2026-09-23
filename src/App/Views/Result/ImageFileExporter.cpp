#include "ImageFileExporter.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>

QString ImageFileExporter::suggestedFileName(const QUrl &source) const
{
    const auto name = QFileInfo(source.toLocalFile()).fileName();
    return name.isEmpty() ? QStringLiteral("Dreamscapes.png") : name;
}

bool ImageFileExporter::save(const QUrl &source, const QUrl &destination)
{
    const auto fail = [this](const QString &message) {
        emit failed(message);
        return false;
    };
    if (!source.isLocalFile() || !QFileInfo(source.toLocalFile()).isFile())
        return fail(tr("The original image file could not be found."));
    QString target;
    if (destination.isLocalFile()) target = destination.toLocalFile();
#ifdef Q_OS_ANDROID
    // Android's system picker grants access to a document URI, not a file path.
    else if (destination.scheme() == "content") target = destination.toString();
#endif
    if (target.isEmpty()) return fail(tr("Choose a location to save the file."));

    QFile input(source.toLocalFile());
    if (!input.open(QIODevice::ReadOnly))
        return fail(tr("Could not read the original image: %1").arg(input.errorString()));
    QImageReader reader(&input);
    if (!reader.canRead() || !input.seek(0))
        return fail(tr("The source file is not a readable image."));

    if (destination.isLocalFile() && QFileInfo(target).canonicalFilePath() == QFileInfo(input).canonicalFilePath()) {
        emit saved(destination);
        return true;
    }
    QSaveFile output(target);
#ifdef Q_OS_ANDROID
    // Document providers cannot publish by renaming a sibling temporary file.
    output.setDirectWriteFallback(destination.scheme() == "content");
#endif
    if (!output.open(QIODevice::WriteOnly))
        return fail(tr("Could not save the file: %1").arg(output.errorString()));
    while (!input.atEnd()) {
        const auto bytes = input.read(1024 * 1024);
        if (input.error() != QFileDevice::NoError) {
            output.cancelWriting();
            return fail(tr("Could not read the original image: %1").arg(input.errorString()));
        }
        if (output.write(bytes) != bytes.size()) {
            const auto message = output.errorString();
            output.cancelWriting();
            return fail(tr("Could not save the file: %1").arg(message));
        }
    }
    if (!output.commit())
        return fail(tr("Could not finish saving the file: %1").arg(output.errorString()));
    emit saved(destination);
    return true;
}
