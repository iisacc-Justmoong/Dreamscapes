#pragma once
#include "../../ViewModels/Native/CanvasDraft.h"
#include "CanvasPresets.h"
class NewCanvasModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(CanvasPresets *catalogue READ catalogue CONSTANT)
    Q_PROPERTY(int categoryIndex READ categoryIndex WRITE setCategoryIndex NOTIFY categoryIndexChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool custom READ custom WRITE setCustom NOTIFY customChanged)
    Q_PROPERTY(QVariantMap selectedPreset READ selectedPreset WRITE setSelectedPreset NOTIFY selectedPresetChanged)
    Q_PROPERTY(QString draftWidth READ draftWidth WRITE setDraftWidth NOTIFY draftWidthChanged)
    Q_PROPERTY(QString draftHeight READ draftHeight WRITE setDraftHeight NOTIFY draftHeightChanged)
    Q_PROPERTY(QString unit READ unit WRITE setUnit NOTIFY unitChanged)
    Q_PROPERTY(QString resolution READ resolution WRITE setResolution NOTIFY resolutionChanged)
    Q_PROPERTY(QString canvasBackground READ canvasBackground WRITE setCanvasBackground NOTIFY canvasBackgroundChanged)
    Q_PROPERTY(QString creationError READ creationError WRITE setCreationError NOTIFY creationErrorChanged)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY sectionsChanged)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY resultCountChanged)
    Q_PROPERTY(QVariantMap specification READ specification NOTIFY specificationChanged)
    Q_PROPERTY(QString selectionName READ selectionName NOTIFY selectionNameChanged)
    Q_PROPERTY(QString dimensionText READ dimensionText NOTIFY dimensionTextChanged)
    Q_PROPERTY(QString aspectRatio READ aspectRatio NOTIFY aspectRatioChanged)
  public:
    explicit NewCanvasModel(QObject *parent = nullptr);
    CanvasPresets *catalogue() {
        return &catalogue_;
    }
    int categoryIndex() const {
        return state_.value("categoryIndex").toInt();
    }
    QString query() const {
        return state_.value("query").toString();
    }
    bool custom() const {
        return state_.value("custom").toBool();
    }
    QVariantMap selectedPreset() const {
        return state_.value("selectedPreset").toMap();
    }
    QString draftWidth() const {
        return state_.value("draftWidth").toString();
    }
    QString draftHeight() const {
        return state_.value("draftHeight").toString();
    }
    QString unit() const {
        return state_.value("unit").toString();
    }
    QString resolution() const {
        return state_.value("resolution").toString();
    }
    QString canvasBackground() const {
        return state_.value("canvasBackground").toString();
    }
    QString creationError() const {
        return state_.value("creationError").toString();
    }
    QVariantList sections() const {
        return state_.value("sections").toList();
    }
    int resultCount() const {
        return state_.value("resultCount").toInt();
    }
    QVariantMap specification() const {
        return state_.value("specification").toMap();
    }
    QString selectionName() const {
        return state_.value("selectionName").toString();
    }
    QString dimensionText() const {
        return state_.value("dimensionText").toString();
    }
    QString aspectRatio() const {
        return state_.value("aspectRatio").toString();
    }
    void setCategoryIndex(int);
    void setQuery(QString);
    void setCustom(bool);
    void setSelectedPreset(QVariantMap);
    void setDraftWidth(QString);
    void setDraftHeight(QString);
    void setUnit(QString);
    void setResolution(QString);
    void setCanvasBackground(QString);
    void setCreationError(QString);
    Q_INVOKABLE void choose(const QVariant &preset);
    Q_INVOKABLE void chooseCategory(int index);
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void customSize();
    Q_INVOKABLE void begin();
    Q_INVOKABLE void changeUnit(const QString &unit);
    Q_INVOKABLE void create();
  signals:
    void canvasRequested(QVariantMap specification);
    void categoryIndexChanged();
    void queryChanged();
    void customChanged();
    void selectedPresetChanged();
    void draftWidthChanged();
    void draftHeightChanged();
    void unitChanged();
    void resolutionChanged();
    void canvasBackgroundChanged();
    void creationErrorChanged();
    void sectionsChanged();
    void resultCountChanged();
    void specificationChanged();
    void selectionNameChanged();
    void dimensionTextChanged();
    void aspectRatioChanged();

  private:
    void send(dreamscapes::vm::CanvasDraftAction, const QString &, const QVariant &);
    void refresh(const dreamscapes::vm::Value &);
    CanvasPresets catalogue_;
    dreamscapes::vm::CanvasDraft model_;
    QVariantMap state_;
};
