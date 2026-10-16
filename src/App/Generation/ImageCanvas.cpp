#include "ImageCanvas.h"
#include <iiSocietyHelper.h>

namespace dreamscapes {
iiSharedCanvas::MediaDocumentResult readImageCanvas(const QString &source) {
    iiSocietyHelper::FileSystem files;
    return files.readCanvas(source);
}
QString persistImageCanvas(const QString &source, QString *error, const QString &containerPath) {
    iiSocietyHelper::FileSystem files;
    const bool opened = containerPath.isEmpty() ? files.openForSource(source) : files.open(containerPath);
    const auto alias = opened ? files.importCanvas(source) : QVariantMap{};
    if (error) *error = files.errorString();
    return alias.value("path").toString();
}
bool storeImageCanvas(const QString &source, const QString &destination, QString *error) {
    iiSocietyHelper::FileSystem files;
    files.openForSource(destination);
    const auto alias = files.createImageCanvas(source, destination);
    if (error) *error = files.errorString();
    return alias.value("available").toBool();
}
QString canvasDestination(const QString &path, QString *error) {
    iiSocietyHelper::FileSystem files;
    const auto selected = files.canvasDestination(path);
    if (error) *error = files.errorString();
    return selected;
}
QString imageCanvasPreview(const QString &source, const QString &, QString *error) {
    iiSocietyHelper::FileSystem files;
    const auto preview = files.openForSource(source) ? files.preview(source) : QUrl{};
    if (error) *error = files.errorString();
    return preview.toLocalFile();
}
}
