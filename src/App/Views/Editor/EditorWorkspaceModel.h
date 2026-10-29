#pragma once
#include "../../ViewModels/Native/EditorCommands.h"
#include "EditorImageImportRequest.h"
#include "EditorProject.h"
#include "EditorToolStateModel.h"
class GenerationController;
class EditorWorkspaceModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(EditorImageImportRequest *imageImporter READ imageImporter WRITE setImageImporter NOTIFY bindingsChanged)
    Q_PROPERTY(EditorProject *project READ project WRITE setProject NOTIFY bindingsChanged)
    Q_PROPERTY(EditorToolStateModel *toolState READ toolState WRITE setToolState NOTIFY bindingsChanged)
    Q_PROPERTY(QObject *generation READ generation WRITE setGeneration NOTIFY bindingsChanged)
    Q_PROPERTY(QVariantMap generationResult READ generationResult WRITE setGenerationResult NOTIFY metadataChanged)
    Q_PROPERTY(QVariantList generationResults READ generationResults WRITE setGenerationResults NOTIFY metadataChanged)
    Q_PROPERTY(QVariantMap generationResultsByArtboard READ generationResultsByArtboard NOTIFY metadataChanged)
    Q_PROPERTY(QVariantMap activeGenerationResult READ activeGenerationResult NOTIFY metadataChanged)
    Q_PROPERTY(bool submitting READ submitting NOTIFY generationChanged)
    Q_PROPERTY(QStringList editorJobIds READ editorJobIds NOTIFY generationChanged)
    Q_PROPERTY(QString generationOperation READ generationOperation NOTIFY generationChanged)
    Q_PROPERTY(QObject *generationTargetCanvas READ generationTargetCanvas NOTIFY generationChanged)
    Q_PROPERTY(QVariantList completedResults READ completedResults NOTIFY generationChanged)
    Q_PROPERTY(QVariantMap previewData READ previewData WRITE setPreviewData NOTIFY previewChanged)
    Q_PROPERTY(QString previewTool READ previewTool WRITE setPreviewTool NOTIFY previewChanged)
    Q_PROPERTY(QString previewField READ previewField NOTIFY previewChanged)
    Q_PROPERTY(QVariantMap previewValues READ previewValues NOTIFY previewChanged)
    Q_PROPERTY(int selectedPath READ selectedPath WRITE setSelectedPath NOTIFY previewChanged)
  public:
    explicit EditorWorkspaceModel(QObject *parent = nullptr) : QObject(parent) {}
    EditorImageImportRequest *imageImporter() const {
        return importer_;
    }
    void setImageImporter(EditorImageImportRequest *value) {
        if (importer_ != value) {
            importer_ = value;
            emit bindingsChanged();
        }
    }
    Q_INVOKABLE bool createCanvas(const QVariantMap &);
    Q_INVOKABLE bool openDocument(const QUrl &, bool asCopy);
    Q_INVOKABLE bool openImages(const QVariantList &sources, const QVariantList &entries);
    EditorProject *project() const {
        return project_;
    }
    void setProject(EditorProject *);
    EditorToolStateModel *toolState() const {
        return toolState_;
    }
    void setToolState(EditorToolStateModel *value) {
        toolState_ = value;
        emit bindingsChanged();
    }
    QObject *generation() const;
    void setGeneration(QObject *);
    QVariantMap generationResult() const {
        return result_;
    }
    void setGenerationResult(const QVariantMap &);
    QVariantList generationResults() const {
        return results_;
    }
    void setGenerationResults(const QVariantList &);
    QVariantMap generationResultsByArtboard() const {
        return byArtboard_;
    }
    QVariantMap activeGenerationResult() const;
    bool submitting() const {
        return submitting_;
    }
    QStringList editorJobIds() const {
        return ids_;
    }
    QString generationOperation() const {
        return operation_;
    }
    QObject *generationTargetCanvas() const {
        return target_;
    }
    QVariantList completedResults() const;
    QVariantMap previewData() const {
        return preview_;
    }
    void setPreviewData(const QVariantMap &value) {
        if (preview_ != value) {
            preview_ = value;
            emit previewChanged();
        }
    }
    QString previewTool() const {
        return previewTool_;
    }
    void setPreviewTool(QString value) {
        if (previewTool_ != value) {
            previewTool_ = std::move(value);
            emit previewChanged();
        }
    }
    QString previewField() const {
        return previewField_;
    }
    QVariantMap previewValues() const {
        return previewValues_;
    }
    int selectedPath() const {
        return selectedPath_;
    }
    void setSelectedPath(int value) {
        if (selectedPath_ != value) {
            selectedPath_ = value;
            emit previewChanged();
        }
    }
    Q_INVOKABLE void syncGenerationMetadata();
    Q_INVOKABLE void requestSource(const QString &, const QVariantMap &);
    Q_INVOKABLE void showPreview(const QString &, const QString &, const QVariantMap &);
    Q_INVOKABLE void submitGeneration(const QString &, const QString &, const QVariantMap &);
    Q_INVOKABLE bool submitAdvanced(const QVariantMap &);
    Q_INVOKABLE void handleToolAction(const QString &, const QString &, const QVariantMap &);
    Q_INVOKABLE void importSource(const QUrl &, const QString &, const QVariantMap &);
    Q_INVOKABLE void sourceRejected(const QString &);
    Q_INVOKABLE void exportImage(const QUrl &, const QString &);
    Q_INVOKABLE void activatePreview(const QVariant &);
    Q_INVOKABLE void showGenerationResults();
    Q_INVOKABLE bool placeVariation(const QVariant &);
  signals:
    void bindingsChanged();
    void metadataChanged();
    void generationChanged();
    void previewChanged();
    void sourceRequested(QString tool, QVariantMap values, QUrl directory, QStringList filters);
    void previewRequested();
    void previewCloseRequested();
    void openDocumentRequested(bool asCopy, QUrl directory);
    void saveAsRequested(QString name, QUrl directory);
    void exportRequested(QString format, QString name, QUrl directory);
    void paintColorRequested();

  private:
    EditorCanvas *canvas() const {
        return project_ ? project_->currentCanvas() : nullptr;
    }
    dreamscapes::vm::EditorInvocation invocation(const QString &, const QString &, const QVariantMap &) const;
    QPointer<EditorImageImportRequest> importer_;
    QPointer<EditorProject> project_;
    QPointer<EditorToolStateModel> toolState_;
    QPointer<GenerationController> generation_;
    QPointer<EditorCanvas> target_;
    QVariantMap result_, byArtboard_, preview_, previewValues_;
    QVariantList results_;
    QStringList ids_;
    QString operation_, previewTool_, previewField_;
    bool submitting_ = false;
    int selectedPath_ = -1;
    dreamscapes::vm::EditorCommandRouter router_;
};
