#pragma once

#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <functional>

class PhotoLibraryExporter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    using Completion = std::function<void(QString assetIdentifier, QString error)>;
    using SaveOperation = std::function<void(const QString &path, Completion completion)>;

    explicit PhotoLibraryExporter(QObject *parent = nullptr);
    PhotoLibraryExporter(SaveOperation operation, QObject *parent);
    bool supported() const { return bool(m_operation); }
    bool busy() const { return m_busy; }
    Q_INVOKABLE bool save(const QUrl &source);

signals:
    void busyChanged();
    void saved(const QString &assetIdentifier);
    void failed(const QString &message);

private:
    SaveOperation m_operation;
    bool m_busy = false;
};
