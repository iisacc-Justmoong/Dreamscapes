#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;

// Presentation bridge only. iiSharedCanvas owns IISC decoding and composition.
class CanvasPreview : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    using QObject::QObject;
    Q_INVOKABLE QUrl sourceForFile(const QVariantMap &file);
};

namespace dreamscapes {
QUrl canvasPreviewSource(const QVariantMap &file);
void installCanvasPreviewProvider(QQmlEngine &engine);
}
