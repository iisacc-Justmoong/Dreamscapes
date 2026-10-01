#pragma once
#include "ImageParameterCodec.h"
#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class AdvancedImageParameters : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap parameters READ parameters NOTIFY parametersChanged)
    Q_PROPERTY(QVariantList schema READ schema CONSTANT)
    Q_PROPERTY(QStringList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
public:
    explicit AdvancedImageParameters(QObject *parent = nullptr);
    explicit AdvancedImageParameters(QString presetFile, QObject *parent = nullptr);
    QVariantMap parameters() const;
    QVariantList schema() const;
    QStringList presets() const;
    QString errorString() const;
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
private:
    bool fail(const QString &error);
    bool updateItem(const QString &collection, const QString &id, const QVariantMap &patch, bool remove = false);
    bool readPresets(QVariantMap *presets);
    bool writePresets(const QVariantMap &presets);
    iiLocalDiffusion::ImageParameters m_parameters = iiLocalDiffusion::ImageParameters::defaults();
    QString m_presetFile, m_error;
    QVariantMap m_presets;
};
