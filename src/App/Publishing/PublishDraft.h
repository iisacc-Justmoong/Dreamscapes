#pragma once
#include "../Views/Editor/EditorProject.h"
#include <QAbstractListModel>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <string>
#include <optional>

// UI edits remain in memory. Persistence uses the common native canvas model;
// legacy JSON annotations are read only when importing an older draft.
class PublishCanvasModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Role { CanvasId = Qt::UserRole + 1, CanvasName, SourceIndex, CanvasWidth, CanvasHeight, Caption, AltText };
    struct Row { QString id, name, caption, alt; int page, width, height; };
    explicit PublishCanvasModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(rows.size()); }
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
private:
    friend class PublishDraft;
    std::vector<Row> rows;
    void replace(std::vector<Row>);
};

class PublishDraft : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(PublishCanvasModel *canvases READ canvases CONSTANT)
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY changed)
public:
    explicit PublishDraft(QObject *parent = nullptr);
    PublishCanvasModel *canvases() { return &m_canvases; }
    QVariantMap values() const { return m_values; }
    int count() const { return m_canvases.rowCount(); }
    bool dirty() const { return m_dirty; }
    QString path() const { return m_path; }
    QString error() const { return m_error; }
    QString sourceName() const { return m_sourceName; }
    EditorProject *project() const { return m_project; }
    Q_INVOKABLE QVariantMap destinationSettings(const QString &platform) const;
    Q_INVOKABLE bool editDestination(const QString &platform, const QString &key, const QVariant &value);
    Q_INVOKABLE bool resetDestination(const QString &platform, const QString &key = {});
    bool editDestinationSettings(const QString &platform, const QVariantMap &values);
    Q_INVOKABLE void begin(EditorProject *project);
    Q_INVOKABLE bool edit(const QString &key, const QVariant &value);
    Q_INVOKABLE bool editCanvas(const QString &id, const QString &key, const QString &value);
    Q_INVOKABLE bool moveCanvas(int index, int direction);
    Q_INVOKABLE QVariantMap canvas(int index) const;
    Q_INVOKABLE bool save(const QUrl &destination);
    Q_INVOKABLE bool load(const QUrl &source);
signals:
    void changed();
private:
    PublishCanvasModel m_canvases;
    QPointer<EditorCanvas> m_source;
    QPointer<EditorProject> m_project;
    QVariantMap m_values;
    QString m_path, m_error, m_sourceName;
    std::string m_savedCanvasHash;
    std::optional<iiSharedCanvas::PublicationMetadata> m_metadata;
    bool m_dirty = false;
    bool fail(const QString &);
    void edited();
    bool applyMetadata(const std::optional<iiSharedCanvas::PublicationMetadata> &metadata);
    iiSharedCanvas::PublicationMetadata metadataForSave() const;
};
