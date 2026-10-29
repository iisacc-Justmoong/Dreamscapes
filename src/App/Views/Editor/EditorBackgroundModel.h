#pragma once
#include "../ColorManagerModel.h"
#include "EditorToolStateModel.h"
#include "../../ViewModels/Native/BackgroundPalette.h"
class EditorBackgroundModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(EditorToolStateModel *toolState READ toolState WRITE setToolState NOTIFY toolStateChanged)
    Q_PROPERTY(ColorManagerModel *colorModel READ colorModel CONSTANT)
    Q_PROPERTY(QVariantList gradientPresets READ gradientPresets NOTIFY paletteChanged)
  public:
    explicit EditorBackgroundModel(QObject *parent = nullptr);
    EditorToolStateModel *toolState() const {
        return owner_;
    }
    void setToolState(EditorToolStateModel *);
    ColorManagerModel *colorModel() {
        return &color_;
    }
    Q_INVOKABLE void syncColor();
    Q_INVOKABLE void changeStop(int index, const QString &field, const QVariant &);
    Q_INVOKABLE void applyFill();
    Q_INVOKABLE void resetColor();
    Q_INVOKABLE void addSwatch();
    Q_INVOKABLE void addStop();
    Q_INVOKABLE void removeStop(int index);
    Q_INVOKABLE void reverseStops();
    Q_INVOKABLE void selectGradient(const QVariant &);
    QVariantList gradientPresets() const;
    Q_INVOKABLE void saveGradient();
    Q_INVOKABLE void runRemoval();
    Q_INVOKABLE void refineRemoval(const QString &, const QVariant &);
    Q_INVOKABLE void resetEdges();
    Q_INVOKABLE void resetRemoval();
    Q_INVOKABLE void applyRemoval(bool asMask);
    Q_INVOKABLE void chooseReplacement(const QUrl &, const QString &kind);
    Q_INVOKABLE void generateReplacement();
    Q_INVOKABLE void resetReplacement();
    Q_INVOKABLE void applyReplacement();
    Q_INVOKABLE void maskAction(const QString &);
    Q_INVOKABLE void maskProperty(const QString &, const QVariant &);
    Q_INVOKABLE void maskCollectionAction(const QString &);
    Q_INVOKABLE void resetMask();
  signals:
    void toolStateChanged();
    void paletteChanged();
    void actionRequested(QString toolId, QString fieldId, QVariantMap values);

  private:
    QVariant value(const QString &, const QVariant &fallback = {}) const;
    QVariantMap values() const;
    EditorCanvas *engine() const;
    void edit(const QString &, const QVariant &);
    void defaults(const char *group);
    void report(bool);
    QPointer<EditorToolStateModel> owner_;
    ColorManagerModel color_;
    dreamscapes::vm::BackgroundPalette palette_;
    bool syncing_ = false;
};
