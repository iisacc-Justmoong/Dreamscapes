#pragma once
#include "Native/WorkspaceState.h"
#include <QPointer>
#include <QVariant>
#include <QUrl>
class GenerationController;
class ImageOpenRequest;
class EditorProject;
class EditorImageImportRequest;
namespace dreamscapes {
class WorkspaceSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *generation READ generation WRITE setGeneration NOTIFY generationChanged)
    Q_PROPERTY(QObject *editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(QObject *imageOpen READ imageOpen WRITE setImageOpen NOTIFY imageOpenChanged)
    Q_PROPERTY(bool resultVisible READ resultVisible WRITE setResultVisible NOTIFY resultVisibleChanged)
    Q_PROPERTY(bool editorVisible READ editorVisible WRITE setEditorVisible NOTIFY editorVisibleChanged)
    Q_PROPERTY(bool publishVisible READ publishVisible WRITE setPublishVisible NOTIFY publishVisibleChanged)
    Q_PROPERTY(QString storageBrowserSection READ storageBrowserSection WRITE setStorageBrowserSection NOTIFY storageBrowserSectionChanged)
    Q_PROPERTY(QString pendingFileKey READ pendingFileKey NOTIFY pendingChanged)
    Q_PROPERTY(QString pendingFileSection READ pendingFileSection NOTIFY pendingChanged)
    Q_PROPERTY(QString pendingFileContainerId READ pendingFileContainerId NOTIFY pendingChanged)
    Q_PROPERTY(QVariant pendingCanvasOpen READ pendingCanvasOpen WRITE setPendingCanvasOpen NOTIFY pendingChanged)
    Q_PROPERTY(QUrl canvasImageSource READ canvasImageSource NOTIFY canvasChanged)
    Q_PROPERTY(QVariantMap canvasSpecification READ canvasSpecification WRITE setCanvasSpecification NOTIFY canvasChanged)
    Q_PROPERTY(QVariantMap canvasGenerationResult READ canvasGenerationResult NOTIFY canvasChanged)
    Q_PROPERTY(QVariantList canvasGenerationResults READ canvasGenerationResults NOTIFY canvasChanged)
    Q_PROPERTY(QVariantMap currentResult READ currentResult WRITE setCurrentResult NOTIFY currentResultChanged)
    Q_PROPERTY(QStringList resultJobIds READ resultJobIds WRITE setResultJobIds NOTIFY resultJobIdsChanged)
    Q_PROPERTY(QString generationRequestError READ generationRequestError WRITE setGenerationRequestError NOTIFY generationRequestErrorChanged)
public:
    explicit WorkspaceSession(QObject *parent = nullptr);
    QObject *generation() const; void setGeneration(QObject *);
    QObject *editor() const { return editor_; } void setEditor(QObject *);
    QObject *imageOpen() const; void setImageOpen(QObject *);
    bool resultVisible() const; void setResultVisible(bool);
    bool editorVisible() const; void setEditorVisible(bool);
    bool publishVisible() const; void setPublishVisible(bool);
    QString storageBrowserSection() const; void setStorageBrowserSection(QString);
    QString pendingFileKey() const; QString pendingFileSection() const; QString pendingFileContainerId() const;
    QVariant pendingCanvasOpen() const; void setPendingCanvasOpen(const QVariant &);
    QUrl canvasImageSource() const;
    QVariantMap canvasSpecification() const; void setCanvasSpecification(const QVariantMap &);
    QVariantMap canvasGenerationResult() const; QVariantList canvasGenerationResults() const;
    QVariantMap currentResult() const; void setCurrentResult(const QVariantMap &);
    QStringList resultJobIds() const; void setResultJobIds(const QStringList &);
    QString generationRequestError() const; void setGenerationRequestError(QString);
    Q_INVOKABLE QObject *ensureEditor();
    Q_INVOKABLE bool openCanvas(const QUrl &, const QVariantMap &result = {});
    Q_INVOKABLE bool finishCanvasOpen(const QUrl &, const QVariantMap &result = {});
    Q_INVOKABLE bool createCanvas(const QVariantMap &);
    Q_INVOKABLE bool openImages(const QVariantList &);
    Q_INVOKABLE bool openPreparedImages(const QVariantList &, const QVariantList &);
    Q_INVOKABLE bool finishPreparedImages(const QVariantList &, const QVariantList &);
    Q_INVOKABLE void documentOpened(const QUrl &);
    Q_INVOKABLE void documentOpenFailed(const QString &);
    Q_INVOKABLE void openHomeFileDialog();
    Q_INVOKABLE void closeCanvas();
    Q_INVOKABLE void openNewCanvas();
    Q_INVOKABLE void presentLatestResult(const QVariantList &, const QString &prompt);
    Q_INVOKABLE void dismissResult();
    Q_INVOKABLE void cancelSubmission();
    Q_INVOKABLE void showSubmission(const QStringList &);
    Q_INVOKABLE void openGenerationReview();
    Q_INVOKABLE bool openHomeFile(const QVariantMap &);
    Q_INVOKABLE void cancelFileOpen();
    Q_INVOKABLE void downloaded(const QVariantMap &);
    Q_INVOKABLE void showStorage(const QString &);
    Q_INVOKABLE bool reportUnavailableAction(const QString &);
signals:
    void generationChanged(); void editorChanged(); void imageOpenChanged();
    void resultVisibleChanged(); void editorVisibleChanged(); void publishVisibleChanged(); void storageBrowserSectionChanged();
    void pendingChanged(); void canvasChanged(); void currentResultChanged(); void resultJobIdsChanged(); void generationRequestErrorChanged();
    void editorRequested(); void inputsDismissRequested(); void canvasFocusRequested();
    void reviewResetRequested(); void reviewFocusRequested(); void homeRequested();
    void newCanvasRequested(); void openFileDialogRequested();
    void storageRequested(QString section);
    void manualResultRequested(QVariantMap file); void promptCompletionRequested(QString prompt, QString aspectRatio);
private:
    void change(const QVariantMap &);
    EditorProject *project() const;
    EditorImageImportRequest *importer() const;
    bool storageVisible() const;
    QPointer<GenerationController> generation_;
    QPointer<QObject> editor_;
    QPointer<ImageOpenRequest> imageOpen_;
    vm::WorkspaceState model_;
    QVariantMap state_;
    bool cancelling_ = false;
};
} // namespace dreamscapes
