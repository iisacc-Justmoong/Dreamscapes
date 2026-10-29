#pragma once
#include "../../ViewModels/Native/GraphicDraft.h"
#include "EditorGraphicCommand.h"
#include "EditorGraphicHelper.h"
#include "EditorToolStateModel.h"
#include <QColor>
#include <QUrl>
class EditorGraphicDraftModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(dreamscapes::ViewData *catalog READ catalog WRITE setCatalog NOTIFY bindingsChanged)
    Q_PROPERTY(EditorToolStateModel *toolState READ toolState WRITE setToolState NOTIFY bindingsChanged)
    Q_PROPERTY(QString currentViewId READ currentViewId WRITE setCurrentViewId NOTIFY viewChanged)
    Q_PROPERTY(QVariant selectedView READ selectedView NOTIFY viewChanged)
    Q_PROPERTY(QVariantList viewCatalog READ viewCatalog NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap collectionIndices READ collectionIndices WRITE setCollectionIndices NOTIFY valuesChanged)
    Q_PROPERTY(QString operationStatus READ operationStatus WRITE setOperationStatus NOTIFY operationChanged)
    Q_PROPERTY(QString resultMessage READ resultMessage WRITE setResultMessage NOTIFY operationChanged)
    Q_PROPERTY(QUrl previewImage READ previewImage NOTIFY operationChanged)
    Q_PROPERTY(QUrl originalImage READ originalImage NOTIFY operationChanged)
    Q_PROPERTY(bool samplingColor READ samplingColor NOTIFY operationChanged)
    Q_PROPERTY(bool imageBusy READ imageBusy NOTIFY operationChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY operationChanged)
    Q_PROPERTY(quint64 revision READ revision NOTIFY valuesChanged)
  public:
    explicit EditorGraphicDraftModel(QObject *parent = nullptr);
    dreamscapes::ViewData *catalog() const {
        return catalog_;
    }
    void setCatalog(dreamscapes::ViewData *);
    EditorToolStateModel *toolState() const {
        return toolState_;
    }
    void setToolState(EditorToolStateModel *);
    QString currentViewId() const;
    void setCurrentViewId(const QString &);
    QVariant selectedView() const;
    QVariantList viewCatalog() const;
    QVariantMap collectionIndices() const;
    void setCollectionIndices(const QVariantMap &);
    quint64 revision() const {
        return revision_;
    }
    QString operationStatus() const {
        return operationStatus_;
    }
    void setOperationStatus(QString value) {
        operationStatus_ = std::move(value);
        emit operationChanged();
    }
    QString resultMessage() const {
        return resultMessage_;
    }
    void setResultMessage(QString value) {
        resultMessage_ = std::move(value);
        emit operationChanged();
    }
    QUrl previewImage() const {
        return previewImage_;
    }
    QUrl originalImage() const {
        return originalImage_;
    }
    bool samplingColor() const {
        return samplingColor_;
    }
    bool imageBusy() const {
        return image_.state().value("pending").toBool();
    }
    bool busy() const {
        return imageBusy() || helper_.state().value("pending").toBool();
    }
    Q_INVOKABLE void activate(const QVariant &option, const QVariant &row);
    Q_INVOKABLE void execute(const QString &action, const QString &operation = {}, const QVariant &parameters = {});
    Q_INVOKABLE void acceptSource(const QUrl &);
    Q_INVOKABLE void acceptColor(const QColor &);
    Q_INVOKABLE void commitField(const QVariant &);
    Q_INVOKABLE QVariantMap helperContext() const;
    Q_INVOKABLE QVariantMap imageContext() const;
    Q_INVOKABLE QString defaultView() const;
    Q_INVOKABLE void restoreView();
    Q_INVOKABLE bool selectView(const QString &);
    Q_INVOKABLE QString operationFor(const QVariant &) const;
    Q_INVOKABLE QVariantList rowsOf(const QVariant &) const;
    Q_INVOKABLE QVariantMap parameters(const QString &) const;
    Q_INVOKABLE QString parameterPath(const QVariant &) const;
    Q_INVOKABLE QVariant value(const QVariant &) const;
    Q_INVOKABLE QVariant collectionDefinition() const;
    Q_INVOKABLE QVariantList collectionItems() const;
    Q_INVOKABLE QVariant collectionItem() const;
    Q_INVOKABLE void selectCollection(int);
    Q_INVOKABLE void addCollectionItem();
    Q_INVOKABLE void removeCollectionItem();
    Q_INVOKABLE void edit(const QVariant &, const QVariant &);
    Q_INVOKABLE void resetView();
    Q_INVOKABLE void applyReturnedParameters(const QString &, const QVariant &);
    Q_INVOKABLE QVariant statusFor(const QString &) const;
    Q_INVOKABLE QVariantList curvePoints(const QVariant &) const;
    Q_INVOKABLE QColor swatchColor() const;
    Q_INVOKABLE QVariant curveChannel() const;
  signals:
    void operationChanged();
    void sheetsCloseRequested();
    void sourceRequested();
    void colorRequested(QColor color);
    void previewRequested();
    void operationSheetRequested();
    void closeToolRequested();
    void reopenToolRequested();
    void bindingsChanged();
    void viewChanged();
    void valuesChanged();

  private:
    void initialize();
    EditorCanvas *engine() const;
    void syncContexts();
    void syncEngine();
    void finishHelper();
    void finishImage();
    void finishSample();
    EditorGraphicHelper helper_;
    EditorGraphicCommand image_;
    QPointer<EditorCanvas> observedEngine_, helperOrigin_, imageOrigin_;
    QString operationStatus_, resultMessage_, sourceOperation_, sourceParameter_, pickerApi_, imageActionName_,
        imageApi_;
    QVariant pickerRow_;
    QUrl previewImage_, originalImage_;
    bool helperAction_ = false, imageAction_ = false, preparingHelper_ = false, samplingColor_ = false;
    qulonglong sampleSequence_ = 0, imageEpoch_ = 0;
    bool send(dreamscapes::vm::GraphicAction, std::initializer_list<QVariant>);
    QPointer<dreamscapes::ViewData> catalog_;
    QPointer<EditorToolStateModel> toolState_;
    std::unique_ptr<dreamscapes::vm::GraphicDraft> model_;
    bool dispatching_ = false;
    quint64 revision_ = 0;
};
