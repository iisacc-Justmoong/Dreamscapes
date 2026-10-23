#pragma once
#include <iiSharedCanvas/Media/MediaIo.h>
#include <QString>
#include <QUrl>

namespace dreamscapes {
// Consumer bridge: existing SDK owns the canvas model, pixels and serialization.
iiSharedCanvas::MediaDocumentResult readImageCanvas(const QString &source);
bool storeImageCanvas(const QString &source, const QString &destination, QString *error);
// Managed native documents belong to Society Files. An explicit selection wins;
// otherwise use the source's Society drive, then the configured default drive.
QString persistImageCanvas(const QString &source, QString *error, const QString &containerPath = {});
// Validate an explicit save/export path. Within Society, .iisc belongs to Files.
QString imageCanvasPreview(const QString &source, const QString &directory, QString *error);
// Lazy UI source; building it performs no file access or canvas decoding.
QUrl imageCanvasPreviewSource(const QString &path, const QString &version);
}
