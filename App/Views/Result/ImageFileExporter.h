#pragma once

#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class ImageFileExporter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit ImageFileExporter(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE QString suggestedFileName(const QUrl &source) const;
    Q_INVOKABLE bool save(const QUrl &source, const QUrl &destination);

signals:
    void saved(const QUrl &destination);
    void failed(const QString &message);
};
