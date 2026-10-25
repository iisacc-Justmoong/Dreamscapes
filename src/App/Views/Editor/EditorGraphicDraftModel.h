#pragma once
#include "../../ViewModels/Native/GraphicDraft.h"
#include "EditorToolStateModel.h"
#include <QColor>
class EditorGraphicDraftModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(dreamscapes::ViewData *catalog READ catalog WRITE setCatalog NOTIFY bindingsChanged)
    Q_PROPERTY(EditorToolStateModel *toolState READ toolState WRITE setToolState NOTIFY bindingsChanged)
    Q_PROPERTY(QString currentViewId READ currentViewId WRITE setCurrentViewId NOTIFY viewChanged)
    Q_PROPERTY(QVariant selectedView READ selectedView NOTIFY viewChanged)
    Q_PROPERTY(QVariantList viewCatalog READ viewCatalog NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap collectionIndices READ collectionIndices WRITE setCollectionIndices NOTIFY valuesChanged)
    Q_PROPERTY(quint64 revision READ revision NOTIFY valuesChanged)
public:
    explicit EditorGraphicDraftModel(QObject *parent = nullptr) : QObject(parent) {}
    dreamscapes::ViewData *catalog() const { return catalog_; } void setCatalog(dreamscapes::ViewData *);
    EditorToolStateModel *toolState() const { return toolState_; } void setToolState(EditorToolStateModel *);
    QString currentViewId() const; void setCurrentViewId(const QString &);
    QVariant selectedView() const;
    QVariantList viewCatalog() const;
    QVariantMap collectionIndices() const; void setCollectionIndices(const QVariantMap &);
    quint64 revision() const { return revision_; }
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
    void bindingsChanged(); void viewChanged(); void valuesChanged();
private:
    void initialize();
    bool send(dreamscapes::vm::GraphicAction, std::initializer_list<QVariant>);
    QPointer<dreamscapes::ViewData> catalog_;
    QPointer<EditorToolStateModel> toolState_;
    std::unique_ptr<dreamscapes::vm::GraphicDraft> model_;
    bool dispatching_ = false;
    quint64 revision_ = 0;
};
