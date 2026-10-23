#include "ImageCanvas.h"
#include <iiSocietyHelper.h>
#include <QUrlQuery>

namespace dreamscapes {
QUrl imageCanvasPreviewSource(const QString &path, const QString &version) {
    const auto encoded = path.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QUrl result(QStringLiteral("image://iisc-preview/") + QString::fromLatin1(encoded));
    QUrlQuery query; query.addQueryItem("revision", version); result.setQuery(query);
    return result;
}
iiSharedCanvas::MediaDocumentResult readImageCanvas(const QString &source) {
    iiSocietyHelper::FileSystem files(iiSocietyHelper::FileSystem::Initialization::Deferred);
    return files.readCanvas(source);
}
QString persistImageCanvas(const QString &source, QString *error, const QString &containerPath) {
    iiSocietyHelper::FileSystem files(iiSocietyHelper::FileSystem::Initialization::Deferred);
    const bool opened = containerPath.isEmpty() ? files.openForSource(source) : files.open(containerPath);
    const auto alias = opened ? files.importCanvas(source) : QVariantMap{};
    if (error) *error = files.errorString();
    return alias.value("path").toString();
}
bool storeImageCanvas(const QString &source, const QString &destination, QString *error) {
    iiSocietyHelper::FileSystem files(iiSocietyHelper::FileSystem::Initialization::Deferred);
    files.openForSource(destination);
    const auto alias = files.createImageCanvas(source, destination);
    if (error) *error = files.errorString();
    return alias.value("available").toBool();
}
QString imageCanvasPreview(const QString &source, const QString &, QString *error) {
    iiSocietyHelper::FileSystem files(iiSocietyHelper::FileSystem::Initialization::Deferred);
    const auto preview = files.openForSource(source) ? files.preview(source) : QUrl{};
    if (error) *error = files.errorString();
    return preview.toLocalFile();
}
}
