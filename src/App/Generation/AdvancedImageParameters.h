#pragma once
#include "ImageParameterCodec.h"
#include "../ViewModels/Native/GenerationDraft.h"
#include "../ViewModels/Native/NumberInputs.h"
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class AdvancedImageParameters : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap parameters READ parameters NOTIFY parametersChanged)
    Q_PROPERTY(QVariantList schema READ schema CONSTANT)
    Q_PROPERTY(QStringList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(QObject *generation READ generation WRITE setGeneration NOTIFY generationChanged)
    Q_PROPERTY(QVariantMap invalidInputs READ invalidInputs NOTIFY invalidInputsChanged)
    Q_PROPERTY(bool hasInvalidInputs READ hasInvalidInputs NOTIFY invalidInputsChanged)
public:
    explicit AdvancedImageParameters(QObject *parent = nullptr);
    explicit AdvancedImageParameters(QString presetFile, QObject *parent = nullptr);
    QVariantMap parameters() const;
    QVariantList schema() const;
    QStringList presets() const;
    QString errorString() const;
    QObject *generation() const { return m_generation; }
    void setGeneration(QObject *generation) { if (m_generation != generation) { m_generation = generation; emit generationChanged(); } }
    QVariantMap invalidInputs() const { return m_invalidInputs; }
    bool hasInvalidInputs() const { return m_validity.invalid(); }
    Q_INVOKABLE bool edit(const QString &, const QVariant &);
    Q_INVOKABLE bool editNumber(const QString &, const QString &);
    Q_INVOKABLE bool editControlWeight(const QString &id, const QString &text);
    Q_INVOKABLE bool editLoraWeight(const QString &id, const QString &text);
    Q_INVOKABLE void recordValidity(const QString &, bool valid);
    Q_INVOKABLE void applyPromptDefaults();
    Q_INVOKABLE void resetDraft();
    Q_INVOKABLE bool loadDraftPreset(const QString &);
    Q_INVOKABLE bool saveDraftPreset(const QString &);
    Q_INVOKABLE bool selectLearnedUpscaler(const QUrl &);
    Q_INVOKABLE bool selectDetailer(const QUrl &);
    Q_INVOKABLE bool selectRefiner(const QUrl &);
    Q_INVOKABLE bool selectIPAdapter(const QString &, const QUrl &, const QUrl &);
    Q_INVOKABLE bool selectPose(const QString &, const QUrl &, const QUrl &);
    Q_INVOKABLE QVariantMap controlNet(const QString &) const;
    Q_INVOKABLE bool addReferenceDocument(const QUrl &);
    Q_INVOKABLE bool selectControlImage(const QString &, const QString &, const QUrl &);
    Q_INVOKABLE int addControlDraft(const QString &process = QStringLiteral("None"));
    Q_INVOKABLE bool addLoraDraft(const QUrl &, const QString &name = {});
    Q_INVOKABLE bool updateParameters(const QVariantMap &patch);
    Q_INVOKABLE void reset();
    Q_INVOKABLE QVariantList submissionIssues(bool desktopWorker = true) const;
    Q_INVOKABLE bool addReferenceImage(const QString &source);
    Q_INVOKABLE bool removeReferenceImage(int index);
    Q_INVOKABLE QString addControlNet(const QString &process = QStringLiteral("None"));
    Q_INVOKABLE bool updateControlNet(const QString &id, const QVariantMap &patch);
    Q_INVOKABLE bool applyControlNet(const QString &id);
    Q_INVOKABLE bool resetControlNet(const QString &id);
    Q_INVOKABLE bool removeControlNet(const QString &id);
    Q_INVOKABLE QString addLora(const QString &source, const QString &name = {});
    Q_INVOKABLE bool updateLora(const QString &id, const QVariantMap &patch);
    Q_INVOKABLE bool removeLora(const QString &id);
    Q_INVOKABLE bool savePreset(const QString &name);
    Q_INVOKABLE bool loadPreset(const QString &name);
    Q_INVOKABLE bool removePreset(const QString &name);
signals:
    void parametersChanged();
    void presetsChanged();
    void errorChanged();
    void generationChanged();
    void invalidInputsChanged();
    void draftRestored();
    void presetSaved(QString name);
    void referenceImageLimitReached();
private:
    dreamscapes::vm::NumberInputs m_numbers;
    bool fail(const QString &error);
    void validity(dreamscapes::vm::ValidityEdit);
    bool updateDraft(const QVariantMap &, const QString &validityKey);
    bool updateItem(const QString &collection, const QString &id, const QVariantMap &patch, bool remove = false);
    bool readPresets(QVariantMap *presets);
    bool writePresets(const QVariantMap &presets);
    iiLocalDiffusion::ImageParameters m_parameters = iiLocalDiffusion::ImageParameters::defaults();
    QString m_presetFile, m_error;
    QVariantMap m_presets;
    QPointer<QObject> m_generation;
    dreamscapes::vm::InputValidity m_validity;
    QVariantMap m_invalidInputs;
};
