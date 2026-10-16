#pragma once
#include "EditorCanvas.h"
#include <QObject>
#include <QCache>
#include <vector>

class EditorProject : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorCanvas *currentCanvas READ currentCanvas NOTIFY currentCanvasChanged)
    Q_PROPERTY(int canvasCount READ canvasCount NOTIFY projectChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY projectChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY projectChanged)
    Q_PROPERTY(QString documentName READ documentName NOTIFY projectChanged)
    Q_PROPERTY(bool isProject READ isProject NOTIFY projectChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap fileInformation READ fileInformation NOTIFY fileInformationChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList artboards READ artboards NOTIFY projectChanged)
    Q_PROPERTY(qulonglong pagePreviewRevision READ pagePreviewRevision NOTIFY pagePreviewsChanged)
public:
    explicit EditorProject(QObject *parent = nullptr);
    ~EditorProject() override;
    EditorCanvas *currentCanvas() const { return m_canvas.get(); }
    int canvasCount() const;
    int currentIndex() const { return m_canvas->activeArtboardIndex(); }
    QVariantList artboards() const;
    qulonglong pagePreviewRevision() const { return m_pagePreviewRevision; }
    Q_INVOKABLE QUrl pagePreviewSource(int index, int maxWidth, int maxHeight);
    QString filePath() const;
    QString documentName() const;
    bool isProject() const { return canvasCount() > 1; }
    bool modified() const { return m_modified; }
    QVariantMap fileInformation() const;
    QString error() const { return m_error.isEmpty() ? currentCanvas()->error() : m_error; }
    Q_INVOKABLE bool setCurrentIndex(int index);
    Q_INVOKABLE void attachCanvas(QQuickItem *surface);
    Q_INVOKABLE bool createCanvas(const QVariantMap &specification);
    Q_INVOKABLE bool openImages(const QVariantList &sources);
    Q_INVOKABLE bool attachImages(const QVariantList &sources);
    Q_INVOKABLE bool openDocumentSource(const QUrl &source, bool asCopy = false);
    Q_INVOKABLE bool saveDocumentAs(const QUrl &destination);
    Q_INVOKABLE bool saveDocument();
signals:
    void currentCanvasChanged();
    void projectChanged();
    void fileInformationChanged();
    void errorChanged();
    void pagePreviewsChanged();
private:
    void invalidatePagePreviews(bool force = false);
    bool fail(const QString &message);
    void replace(std::unique_ptr<EditorCanvas> canvas, int index = 0);
    std::unique_ptr<EditorCanvas> m_canvas;
    QString m_error;
    bool m_modified = false;
    QCache<QString, QUrl> m_pagePreviews{128};
    qulonglong m_pagePreviewRevision = 0;
    qulonglong m_lastPreviewCanvasRevision = 0;
    quint32 m_lastPreviewFrame = 0;
    bool m_lastPreviewDocumentReady = false;
    bool m_previewStateValid = false;
    bool m_previewNotificationPending = false;
};
