#pragma once
#include "../../ViewModels/Native/EditorToolState.h"
#include "../../ViewModels/Native/NumberInputs.h"
#include "../../ViewModels/ViewData.h"
#include <QPointer>
#include <QUrl>
class EditorCanvas;
class EditorToolStateModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(dreamscapes::ViewData *catalog READ catalog WRITE setCatalog NOTIFY catalogChanged)
    Q_PROPERTY(EditorCanvas *engine READ engine WRITE setEngine NOTIFY engineChanged)
    Q_PROPERTY(QVariantMap definition READ definition NOTIFY definitionChanged)
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QString toolId READ toolId WRITE setToolId NOTIFY toolIdChanged)
    Q_PROPERTY(QVariantMap settingsByTool READ settingsByTool WRITE setSettingsByTool NOTIFY settingsByToolChanged)
    Q_PROPERTY(QVariantMap graphicViewsByTool READ graphicViewsByTool WRITE setGraphicViewsByTool NOTIFY
                   graphicViewsByToolChanged)
    Q_PROPERTY(QVariantMap compactViewsByTool READ compactViewsByTool WRITE setCompactViewsByTool NOTIFY
                   compactViewsByToolChanged)
    Q_PROPERTY(QString fileViewId READ fileViewId WRITE setFileViewId NOTIFY fileViewIdChanged)
    Q_PROPERTY(QString elementsViewId READ elementsViewId WRITE setElementsViewId NOTIFY elementsViewIdChanged)
    Q_PROPERTY(bool elementsMultipleSelection READ elementsMultipleSelection WRITE setElementsMultipleSelection NOTIFY
                   elementsMultipleSelectionChanged)
    Q_PROPERTY(QString elementsSelectionScope READ elementsSelectionScope WRITE setElementsSelectionScope NOTIFY
                   elementsSelectionScopeChanged)
    Q_PROPERTY(QString notice READ notice WRITE setNotice NOTIFY noticeChanged)
    Q_PROPERTY(
        QVariantList generationModels READ generationModels WRITE setGenerationModels NOTIFY generationModelsChanged)
    Q_PROPERTY(QVariantMap elementValues READ elementValues NOTIFY elementValuesChanged)
    Q_PROPERTY(QVariantMap textValues READ textValues NOTIFY textValuesChanged)
    Q_PROPERTY(QVariantMap photoValues READ photoValues NOTIFY photoValuesChanged)
    Q_PROPERTY(QVariant activeElementCandidate READ activeElementCandidate NOTIFY activeElementCandidateChanged)
    Q_PROPERTY(QVariantList scopedElementCandidates READ scopedElementCandidates NOTIFY scopedElementCandidatesChanged)
    Q_PROPERTY(QVariantList elementFields READ elementFields NOTIFY elementFieldsChanged)
    Q_PROPERTY(QVariantList compactViewCatalog READ compactViewCatalog NOTIFY compactViewCatalogChanged)
    Q_PROPERTY(QVariant compactSelectedView READ compactSelectedView NOTIFY compactSelectedViewChanged)
    Q_PROPERTY(QString compactCurrentViewId READ compactCurrentViewId NOTIFY compactCurrentViewIdChanged)
    Q_PROPERTY(QVariantList compactPresentedFields READ compactPresentedFields NOTIFY compactPresentedFieldsChanged)
    Q_PROPERTY(QVariantMap layerAdjustmentValues READ layerAdjustmentValues NOTIFY revisionChanged)
    Q_PROPERTY(QString layerAdjustmentKey READ layerAdjustmentKey NOTIFY revisionChanged)
    Q_PROPERTY(QString lastLayerAdjustmentTool READ lastLayerAdjustmentTool NOTIFY revisionChanged)
    Q_PROPERTY(QVariantMap collapsedLayerGroups READ collapsedLayerGroups NOTIFY layerGroupsChanged)
    Q_PROPERTY(quint64 revision READ revision NOTIFY revisionChanged)
  public:
    explicit EditorToolStateModel(QObject *parent = nullptr);
    dreamscapes::ViewData *catalog() const {
        return catalog_;
    }
    void setCatalog(dreamscapes::ViewData *);
    EditorCanvas *engine() const;
    void setEngine(EditorCanvas *);
    QVariantMap definition() const {
        return state_.value("definition").toMap();
    }
    QVariantMap values() const {
        return state_.value("values").toMap();
    }
    QString toolId() const {
        return state_.value("toolId").toString();
    }
    void setToolId(QString);
    QVariantMap settingsByTool() const {
        return state_.value("settingsByTool").toMap();
    }
    void setSettingsByTool(QVariantMap);
    QVariantMap graphicViewsByTool() const {
        return state_.value("graphicViewsByTool").toMap();
    }
    void setGraphicViewsByTool(QVariantMap);
    QVariantMap compactViewsByTool() const {
        return state_.value("compactViewsByTool").toMap();
    }
    void setCompactViewsByTool(QVariantMap);
    QString fileViewId() const {
        return state_.value("fileViewId").toString();
    }
    void setFileViewId(QString);
    QString elementsViewId() const {
        return state_.value("elementsViewId").toString();
    }
    void setElementsViewId(QString);
    bool elementsMultipleSelection() const {
        return state_.value("elementsMultipleSelection").toBool();
    }
    void setElementsMultipleSelection(bool);
    QString elementsSelectionScope() const {
        return state_.value("elementsSelectionScope").toString();
    }
    void setElementsSelectionScope(QString);
    QString notice() const {
        return state_.value("notice").toString();
    }
    void setNotice(QString);
    QVariantList generationModels() const {
        return state_.value("generationModels").toList();
    }
    void setGenerationModels(QVariantList);
    Q_INVOKABLE void syncElementSettings(bool forceShapeView = false);
    Q_INVOKABLE void setValue(const QString &, const QVariant &);
    Q_INVOKABLE void setGraphicValue(const QString &, const QString &, const QVariant &);
    Q_INVOKABLE void setGraphicValues(const QString &, const QVariant &);
    Q_INVOKABLE void replaceGraphicCollection(const QString &, const QString &, const QVariant &);
    Q_INVOKABLE void resetGraphicView(const QString &, const QVariant &);
    Q_INVOKABLE void resetTool();
    QVariantMap layerAdjustmentValues() const {
        return state_.value("layerAdjustmentValues").toMap();
    }
    QString layerAdjustmentKey() const {
        return state_.value("layerAdjustmentKey").toString();
    }
    QString lastLayerAdjustmentTool() const {
        return state_.value("lastLayerAdjustmentTool").toString();
    }
    Q_INVOKABLE void editLayerAdjustment(const QString &, const QVariant &);
    QVariantMap collapsedLayerGroups() const { return state_.value("collapsedLayerGroups").toMap(); }
    Q_INVOKABLE void expandLayerGroup(const QString &id, bool expanded);
    Q_INVOKABLE bool editLayerOpacity(const QString &text);
    Q_INVOKABLE bool openLayerAdjustment(const QVariantMap &entry);
    Q_INVOKABLE void layerAction(const QString &key, bool shadow, bool highlight);
    Q_INVOKABLE QVariantList layerHierarchy(const QUrl &assetRoot, const QVariantMap &collapsed) const;
    Q_INVOKABLE QVariantMap layerAdjustment(const QString &key, const QVariantList &entries) const;
    Q_INVOKABLE QVariantMap layerAdjustmentField(const QString &id) const;
    Q_INVOKABLE void requestElementImageFill();
    Q_INVOKABLE void importPhotoSources(const QString &containerPath);
    Q_INVOKABLE void restoreSettings(const QVariantMap &);
    dreamscapes::vm::Value nativeSnapshot() const {
        return model_ ? model_->inputSnapshot() : dreamscapes::vm::Value();
    }
    void executeNative(const dreamscapes::vm::ToolCommand &);
    quint64 revision() const {
        return revision_;
    }
    Q_INVOKABLE bool elementFieldEnabled(const QString &) const;
    Q_INVOKABLE QString formattedElement(const QString &, int index = -1) const;
    Q_INVOKABLE void selectElementView(const QString &);
    Q_INVOKABLE void editElement(const QString &, const QVariant &);
    Q_INVOKABLE void chooseElementShape(const QVariant &);
    Q_INVOKABLE void resetElementView();
    Q_INVOKABLE void elementSelectionAction(const QString &action);
    Q_INVOKABLE void setElementSelected(const QString &id, bool selected);
    Q_INVOKABLE void commitElementNumber(const QString &, int, const QString &);
    Q_INVOKABLE void selectCompactView(const QString &);
    Q_INVOKABLE void resetCompactView();
    Q_INVOKABLE QVariantMap capability(const QVariant &field) const;
    Q_INVOKABLE void editText(const QString &, const QVariant &);
    Q_INVOKABLE void patchText(const QVariant &);
    Q_INVOKABLE void resetTextView(int index);
    Q_INVOKABLE void applyTextRole(const QVariant &);
    Q_INVOKABLE bool textOptionAvailable(const QString &, const QString &) const;
    Q_INVOKABLE void textLinePreset(const QString &);
    QVariantMap elementValues() const {
        return state_.value("elementValues").toMap();
    }
    QVariantMap textValues() const {
        return state_.value("textValues").toMap();
    }
    QVariantMap photoValues() const {
        return state_.value("photoValues").toMap();
    }
    QVariant activeElementCandidate() const {
        return state_.value("activeElementCandidate");
    }
    QVariantList scopedElementCandidates() const {
        return state_.value("scopedElementCandidates").toList();
    }
    QVariantList elementFields() const {
        return state_.value("elementFields").toList();
    }
    QVariantList compactViewCatalog() const {
        return state_.value("compactViewCatalog").toList();
    }
    QVariant compactSelectedView() const {
        return state_.value("compactSelectedView");
    }
    QString compactCurrentViewId() const {
        return state_.value("compactCurrentViewId").toString();
    }
    QVariantList compactPresentedFields() const {
        return state_.value("compactPresentedFields").toList();
    }
  signals:
    void layerGroupsChanged();
    void closeToolRequested();
    void layerAdjustmentRequested(QString title);
    void layerHistogramRequested();
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
    void revisionChanged();
    void elementValuesChanged();
    void textValuesChanged();
    void photoValuesChanged();
    void activeElementCandidateChanged();
    void scopedElementCandidatesChanged();
    void elementFieldsChanged();
    void compactViewCatalogChanged();
    void compactSelectedViewChanged();
    void compactCurrentViewIdChanged();
    void compactPresentedFieldsChanged();

  private:
    dreamscapes::vm::NumberInputs numberInputs_;
    void send(dreamscapes::vm::ToolAction, std::initializer_list<QVariant>);
    void refresh();
    void configure();
    void scheduleSync();
    QPointer<dreamscapes::ViewData> catalog_;
    QPointer<EditorCanvas> engine_;
    std::unique_ptr<dreamscapes::vm::EditorToolState> model_;
    QVariantMap state_;
    std::vector<dreamscapes::vm::ToolCommand> pendingCommands_;
    quint64 revision_ = 0;
    bool applying_ = false, syncPending_ = false;
};
