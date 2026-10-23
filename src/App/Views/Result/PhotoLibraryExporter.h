#pragma once

#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

class PhotoLibraryExporter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    using Completion = std::function<void(QString assetIdentifier, QString error)>;
    struct Image { QString path; QString mimeType; };
    struct Preparation { Image image; QString error; std::shared_ptr<void> lifetime; };
    // Preparation runs on a worker; dispatch runs on the GUI thread. Resource
    // ownership stays in the request until its first native completion.
    using PrepareOperation = std::function<Preparation(const QUrl &source)>;
    using SaveOperation = std::function<void(const Image &image, Completion completion)>;

    explicit PhotoLibraryExporter(QObject *parent = nullptr);
    PhotoLibraryExporter(SaveOperation operation, QObject *parent);
    PhotoLibraryExporter(PrepareOperation prepare, SaveOperation operation, QObject *parent);
    bool supported() const { return bool(m_prepare) && bool(m_operation); }
    bool busy() const { return m_busy; }
    QString lastRejection() const { return m_rejection; }
    Q_INVOKABLE bool save(const QUrl &source);

signals:
    void busyChanged();
    void saved(const QString &assetIdentifier);
    void failed(const QString &message);
    void rejected(const QString &message);

private:
    bool reject(const QString &message);
    void complete(qulonglong request, QString assetIdentifier, QString error);
    static Preparation prepareImage(const QUrl &source);
    PrepareOperation m_prepare;
    SaveOperation m_operation;
    QString m_rejection;
    qulonglong m_request = 0;
    bool m_busy = false;
};
