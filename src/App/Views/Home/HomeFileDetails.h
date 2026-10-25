#pragma once
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>

// Read-only presentation adapter. Selecting a file never adopts an authoring
// document, refreshes the storage catalog, or starts a generation request.
class HomeFileDetails : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap file READ file WRITE setFile NOTIFY fileChanged)
    Q_PROPERTY(QVariantMap metadata READ metadata NOTIFY metadataChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY metadataChanged)
public:
    explicit HomeFileDetails(QObject *parent = nullptr) : QObject(parent) {}
    ~HomeFileDetails() override { ++*m_request; }
    QVariantMap file() const { return m_file; }
    QVariantMap metadata() const { return m_metadata; }
    bool loading() const { return m_loading; }
    void setFile(const QVariantMap &file);
signals:
    void fileChanged();
    void metadataChanged();
private:
    QVariantMap m_file, m_metadata;
    bool m_loading = false;
    std::shared_ptr<std::atomic_uint64_t> m_request = std::make_shared<std::atomic_uint64_t>(0);
};
