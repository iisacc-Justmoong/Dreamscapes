#pragma once
#include "../../ViewModels/Native/EditorToolState.h"
#include "../../ViewModels/ViewData.h"
#include <QPointer>
class EditorCanvas;
class EditorToolStateModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(dreamscapes::ViewData *catalog READ catalog WRITE setCatalog NOTIFY catalogChanged)
    Q_PROPERTY(EditorCanvas *engine READ engine WRITE setEngine NOTIFY engineChanged)
    Q_PROPERTY(QVariantMap definition READ definition NOTIFY definitionChanged)
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QString toolId READ toolId WRITE setToolId NOTIFY toolIdChanged)
    Q_PROPERTY(QVariantMap settingsByTool READ settingsByTool WRITE setSettingsByTool NOTIFY settingsByToolChanged)
    Q_PROPERTY(QVariantMap graphicViewsByTool READ graphicViewsByTool WRITE setGraphicViewsByTool NOTIFY graphicViewsByToolChanged)
    Q_PROPERTY(QVariantMap compactViewsByTool READ compactViewsByTool WRITE setCompactViewsByTool NOTIFY compactViewsByToolChanged)
    Q_PROPERTY(QString fileViewId READ fileViewId WRITE setFileViewId NOTIFY fileViewIdChanged)
    Q_PROPERTY(QString elementsViewId READ elementsViewId WRITE setElementsViewId NOTIFY elementsViewIdChanged)
    Q_PROPERTY(bool elementsMultipleSelection READ elementsMultipleSelection WRITE setElementsMultipleSelection NOTIFY elementsMultipleSelectionChanged)
    Q_PROPERTY(QString elementsSelectionScope READ elementsSelectionScope WRITE setElementsSelectionScope NOTIFY elementsSelectionScopeChanged)
    Q_PROPERTY(QString notice READ notice WRITE setNotice NOTIFY noticeChanged)
    Q_PROPERTY(QVariantList generationModels READ generationModels WRITE setGenerationModels NOTIFY generationModelsChanged)
public:
    explicit EditorToolStateModel(QObject *parent = nullptr);
    dreamscapes::ViewData *catalog() const { return catalog_; }
    void setCatalog(dreamscapes::ViewData *);
    EditorCanvas *engine() const;
    void setEngine(EditorCanvas *);
    QVariantMap definition() const { return state_.value("definition").toMap(); }
    QVariantMap values() const { return state_.value("values").toMap(); }
    QString toolId() const { return state_.value("toolId").toString(); }
    void setToolId(QString);
    QVariantMap settingsByTool() const { return state_.value("settingsByTool").toMap(); }
    void setSettingsByTool(QVariantMap);
    QVariantMap graphicViewsByTool() const { return state_.value("graphicViewsByTool").toMap(); }
    void setGraphicViewsByTool(QVariantMap);
    QVariantMap compactViewsByTool() const { return state_.value("compactViewsByTool").toMap(); }
    void setCompactViewsByTool(QVariantMap);
    QString fileViewId() const { return state_.value("fileViewId").toString(); }
    void setFileViewId(QString);
    QString elementsViewId() const { return state_.value("elementsViewId").toString(); }
    void setElementsViewId(QString);
    bool elementsMultipleSelection() const { return state_.value("elementsMultipleSelection").toBool(); }
    void setElementsMultipleSelection(bool);
    QString elementsSelectionScope() const { return state_.value("elementsSelectionScope").toString(); }
    void setElementsSelectionScope(QString);
    QString notice() const { return state_.value("notice").toString(); }
    void setNotice(QString);
    QVariantList generationModels() const { return state_.value("generationModels").toList(); }
    void setGenerationModels(QVariantList);
    Q_INVOKABLE void syncElementSettings(bool forceShapeView = false);
    Q_INVOKABLE void setValue(const QString &, const QVariant &);
    Q_INVOKABLE void setGraphicValue(const QString &, const QString &, const QVariant &);
    Q_INVOKABLE void setGraphicValues(const QString &, const QVariant &);
    Q_INVOKABLE void replaceGraphicCollection(const QString &, const QString &, const QVariant &);
    Q_INVOKABLE void resetGraphicView(const QString &, const QVariant &);
    Q_INVOKABLE void resetTool();
    dreamscapes::vm::Value nativeSnapshot() const { return model_ ? model_->presentation() : dreamscapes::vm::Value(); }
    void executeNative(const dreamscapes::vm::ToolCommand &);
signals:
    void catalogChanged();
    void engineChanged();
    void definitionChanged();
    void valuesChanged();
    void valueEdited(QString toolId, QString fieldId, QVariant value);
    void resetRequested(QString toolId, QVariantMap defaults);
    void sourceRequested(QString toolId, QVariantMap values);
    void toolIdChanged();
    void settingsByToolChanged();
    void graphicViewsByToolChanged();
    void compactViewsByToolChanged();
    void fileViewIdChanged();
    void elementsViewIdChanged();
    void elementsMultipleSelectionChanged();
    void elementsSelectionScopeChanged();
    void noticeChanged();
    void generationModelsChanged();
private:
    void send(dreamscapes::vm::ToolAction, std::initializer_list<QVariant>);
    void refresh();
    void configure();
    void scheduleSync();
    QPointer<dreamscapes::ViewData> catalog_;
    QPointer<EditorCanvas> engine_;
    std::unique_ptr<dreamscapes::vm::EditorToolState> model_;
    QVariantMap state_;
    bool applying_ = false, syncPending_ = false;
};
