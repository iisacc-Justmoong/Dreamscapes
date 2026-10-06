#pragma once
#include "EditorCanvas.h"
#include <QObject>
#include <vector>

class EditorProject : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorCanvas *currentCanvas READ currentCanvas NOTIFY currentCanvasChanged)
    Q_PROPERTY(int canvasCount READ canvasCount NOTIFY currentCanvasChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentCanvasChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY projectChanged)
    Q_PROPERTY(QString documentName READ documentName NOTIFY projectChanged)
    Q_PROPERTY(bool isProject READ isProject NOTIFY projectChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY projectChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit EditorProject(QObject *parent = nullptr);
    ~EditorProject() override;
    EditorCanvas *currentCanvas() const { return m_pages.at(m_index).get(); }
    int canvasCount() const { return static_cast<int>(m_pages.size()); }
    int currentIndex() const { return m_index; }
    QString filePath() const;
    QString documentName() const;
    bool isProject() const { return canvasCount() > 1 || !m_path.isEmpty(); }
    bool modified() const { return m_modified; }
    QString error() const { return m_error.isEmpty() ? currentCanvas()->error() : m_error; }
    Q_INVOKABLE bool setCurrentIndex(int index);
    Q_INVOKABLE void attachCanvas(QQuickItem *surface);
    Q_INVOKABLE bool createCanvas(const QVariantMap &specification);
    Q_INVOKABLE bool openImages(const QVariantList &sources);
    Q_INVOKABLE bool openDocumentSource(const QUrl &source, bool asCopy = false);
    Q_INVOKABLE bool saveDocumentAs(const QUrl &destination);
    Q_INVOKABLE bool saveDocument();
signals:
    void currentCanvasChanged();
    void projectChanged();
    void errorChanged();
private:
    bool fail(const QString &message);
    void replace(std::vector<std::unique_ptr<EditorCanvas>> pages, int index = 0, const QString &path = {});
    std::vector<std::unique_ptr<EditorCanvas>> m_pages;
    int m_index = 0;
    QString m_path, m_error;
    bool m_modified = false;
};
