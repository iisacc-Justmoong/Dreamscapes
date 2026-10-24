#pragma once
#include "ImageFileExporter.h"
#include <QVariantList>

class GenerationReviewExport : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(int processed READ processed NOTIFY changed)
    Q_PROPERTY(int succeeded READ succeeded NOTIFY changed)
    Q_PROPERTY(int failed READ failed NOTIFY changed)
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed)
public:
    explicit GenerationReviewExport(QObject* parent = nullptr);
    GenerationReviewExport(ImageFileExporter::SaveOperation operation, QObject* parent);
    bool busy() const { return m_busy; }
    int total() const { return m_total; }
    int processed() const { return m_processed; }
    int succeeded() const { return m_outcome.succeeded; }
    int failed() const { return m_outcome.failed; }
    QVariantMap result() const;
    Q_INVOKABLE bool start(const QVariantList& entries, const QUrl& folder);
signals:
    void changed();
    void completed(const QVariantMap& result);
    void rejected(const QString& message);
private:
    struct Outcome { int succeeded = 0, failed = 0; QUrl directory; QStringList errors; };
    ImageFileExporter::SaveOperation m_operation;
    QFutureWatcher<Outcome> m_completion;
    Outcome m_outcome;
    bool m_busy = false;
    int m_total = 0, m_processed = 0;
};
