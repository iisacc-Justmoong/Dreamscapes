#pragma once

#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace dreamscapes {
// Product-side preview addresses are independent of a QML image provider.
QString canvasFileVersion(const QString &path);
QUrl canvasPreviewSource(const QVariantMap &file);
}
