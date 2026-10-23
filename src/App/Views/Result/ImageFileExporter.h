#pragma once

#include <QObject>
#include <QFutureWatcher>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <functional>

class ImageFileExporter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    using SaveOperation = std::function<QString(const QUrl &, const QUrl &, bool)>;
    explicit ImageFileExporter(QObject *parent = nullptr);
    ImageFileExporter(SaveOperation operation, QObject *parent);
    bool busy() const { return m_busy; }
    QString lastRejection() const { return m_rejection; }
    Q_INVOKABLE QString suggestedFileName(const QUrl &source) const;
    // true accepts asynchronous work; saved/failed is its terminal outcome.
    Q_INVOKABLE bool saveVideo(const QUrl &source, const QUrl &destination);
    Q_INVOKABLE bool save(const QUrl &source, const QUrl &destination);

private:
    bool saveMedia(const QUrl &source, const QUrl &destination, bool video);
    bool reject(const QString &message);
    SaveOperation m_operation;
    QFutureWatcher<QString> m_completion;
    QUrl m_destination;
    QString m_rejection;
    qulonglong m_request = 0;
    bool m_busy = false;
signals:
    void busyChanged();
    void saved(const QUrl &destination);
    void failed(const QString &message);
    void rejected(const QString &message);
};
