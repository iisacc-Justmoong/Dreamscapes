#pragma once
#include "EditorCanvas.h"
#include "EditorPagePreviews.h"
#include "EditorImageImport.h"
#include "EditorDocumentOpen.h"
#include <Container.h>
#include <QObject>
#include <vector>

class EditorDocumentOpenRequest;
class EditorProject : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorCanvas *currentCanvas READ currentCanvas NOTIFY currentCanvasChanged)
    Q_PROPERTY(int canvasCount READ canvasCount NOTIFY projectChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY navigationChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY projectChanged)
    Q_PROPERTY(QString documentName READ documentName NOTIFY projectChanged)
    Q_PROPERTY(bool isProject READ isProject NOTIFY projectChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap fileInformation READ fileInformation NOTIFY fileInformationChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList artboards READ artboards NOTIFY navigationChanged)
    Q_PROPERTY(qulonglong pagePreviewRevision READ pagePreviewRevision NOTIFY pagePreviewsChanged)
    Q_PROPERTY(bool opening READ opening NOTIFY documentOpenChanged)
    Q_PROPERTY(iiSocietyContainer::Container *container READ container WRITE setContainer NOTIFY documentOpenChanged)
public:
    explicit EditorProject(QObject *parent = nullptr);
    EditorProject(EditorPagePreviews::Operation previewOperation, QObject *parent);
    EditorProject(EditorPagePreviews::Operation previewOperation,dreamscapes::DocumentOpenOperation openOperation,QObject *parent);
    ~EditorProject() override;
    EditorCanvas *currentCanvas() const { return m_canvas.get(); }
    int canvasCount() const;
    int currentIndex() const { return m_canvas->activeArtboardIndex(); }
    QVariantList artboards() const;
    qulonglong pagePreviewRevision() const { return m_pagePreviewRevision; }
    // Cached data or empty while optional rendering is pending. Read again on
    // pagePreviewReady(index); this command never waits for pixels or file I/O.
    Q_INVOKABLE QUrl pagePreviewSource(int index, int maxWidth, int maxHeight);
    Q_INVOKABLE void cancelPagePreviews() { m_pagePreviews->cancelPending(); }
    QString filePath() const;
    QString documentName() const;
    bool isProject() const { return canvasCount() > 1; }
    bool modified() const { return m_modified; }
    QVariantMap fileInformation() const;
    QString error() const { return m_error.isEmpty() ? currentCanvas()->error() : m_error; }
    Q_INVOKABLE bool setCurrentIndex(int index);
    Q_INVOKABLE void attachCanvas(QQuickItem *surface);
    Q_INVOKABLE bool createCanvas(const QVariantMap &specification);
    // Native synchronous utilities; GUI imports belong to EditorImageImportRequest.
    bool openImages(const QVariantList &sources);
    bool attachImages(const QVariantList &sources);
    dreamscapes::ImageImportRequest imageImportRequest(const QVariantList &sources,bool attached) const;
    bool adoptImageImport(dreamscapes::ImageImportResult result,bool attached);
    bool adoptDocumentOpen(dreamscapes::DocumentOpenResult result);
    bool opening() const;
    iiSocietyContainer::Container *container() const;
    void setContainer(iiSocietyContainer::Container *container);
    // Return means accepted. documentOpened is emitted only after native adoption.
    Q_INVOKABLE bool openDocumentSource(const QUrl &source, bool asCopy = false);
    Q_INVOKABLE void cancelDocumentOpen();
    Q_INVOKABLE bool saveDocumentAs(const QUrl &destination);
    Q_INVOKABLE bool saveDocument();
signals:
    void currentCanvasChanged();
    void projectChanged();
    void navigationChanged();
    void fileInformationChanged();
    void errorChanged();
    void pagePreviewsChanged();
    void pagePreviewReady(int index);
    void documentOpenChanged();
    void documentOpened(QUrl source);
    void documentOpenFailed(QString message);
    void documentOpenCancelled();
private:
    void invalidatePagePreviews(bool force = false);
    bool fail(const QString &message);
    void replace(std::unique_ptr<EditorCanvas> canvas, int index = 0);
    // View command lifetime spans replacement of the current native canvas.
    std::shared_ptr<EditorFrameExporter> m_frameExporter;
    std::unique_ptr<EditorCanvas> m_canvas;
    std::unique_ptr<EditorDocumentOpenRequest> m_documentOpen;
    QString m_error;
    bool m_modified = false;
    std::unique_ptr<EditorPagePreviews> m_pagePreviews;
    std::shared_ptr<const iiSharedCanvas::Document> m_pageSnapshot;
    std::shared_ptr<const EditorPagePreviews::Visibility> m_pageVisibility;
    qulonglong m_pageSnapshotRevision = 0;
    qulonglong m_pagePreviewRevision = 0;
    qulonglong m_lastPreviewCanvasRevision = 0;
    quint32 m_lastPreviewFrame = 0;
    bool m_lastPreviewDocumentReady = false;
    bool m_previewStateValid = false;
    bool m_previewNotificationPending = false;
    std::vector<bool> m_timeDependentPreviews;
};
