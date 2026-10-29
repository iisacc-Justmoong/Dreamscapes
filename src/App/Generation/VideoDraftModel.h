#pragma once
#include "../ViewModels/Native/GenerationDraft.h"
#include "../ViewModels/VideoTimelineModel.h"
#include <QPointer>
class GenerationController;
class VideoDraftModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *generation READ generation WRITE setGeneration NOTIFY generationChanged)
    Q_PROPERTY(dreamscapes::VideoTimelineModel *timeline READ timeline WRITE setTimeline NOTIFY timelineChanged)
    Q_PROPERTY(QVariantMap values READ values WRITE setValues NOTIFY valuesChanged)
    Q_PROPERTY(QVariantMap invalidInputs READ invalidInputs NOTIFY invalidInputsChanged)
    Q_PROPERTY(bool hasInvalidInputs READ hasInvalidInputs NOTIFY invalidInputsChanged)
    Q_PROPERTY(bool cameraSelectionValid READ cameraSelectionValid NOTIFY valuesChanged)
    Q_PROPERTY(QString aspectRatio READ aspectRatio NOTIFY valuesChanged)
    Q_PROPERTY(QString selectedPreset READ selectedPreset WRITE setSelectedPreset NOTIFY presetChanged)
    Q_PROPERTY(QString presetFeedback READ presetFeedback NOTIFY presetChanged)
  public:
    explicit VideoDraftModel(QObject *parent = nullptr);
    QObject *generation() const;
    void setGeneration(QObject *);
    dreamscapes::VideoTimelineModel *timeline() const {
        return timeline_;
    }
    void setTimeline(dreamscapes::VideoTimelineModel *);
    QVariantMap values() const;
    void setValues(const QVariantMap &);
    QVariantMap invalidInputs() const;
    bool hasInvalidInputs() const;
    bool cameraSelectionValid() const;
    QString aspectRatio() const;
    QString selectedPreset() const {
        return selectedPreset_;
    }
    void setSelectedPreset(QString value) {
        if (selectedPreset_ != value) {
            selectedPreset_ = std::move(value);
            emit presetChanged();
        }
    }
    QString presetFeedback() const {
        return presetFeedback_;
    }
    Q_INVOKABLE QVariantMap defaults() const;
    Q_INVOKABLE bool edit(const QString &, const QVariant &);
    Q_INVOKABLE bool editNumber(const QString &, const QString &);
    Q_INVOKABLE QVariantMap parameters() const;
    Q_INVOKABLE bool savePreset(const QString &);
    Q_INVOKABLE bool loadPreset(const QString &);
    Q_INVOKABLE void resetDraft();
    Q_INVOKABLE bool addImageKey(const QUrl &, int frame, const QString &trackId = {});
  signals:
    void generationChanged();
    void timelineChanged();
    void valuesChanged();
    void invalidInputsChanged();
    void presetChanged();
    void viewResetRequested();

  private:
    void notify(const QVariantMap &oldValues, const QVariantMap &oldInvalid);
    QPointer<GenerationController> generation_;
    QPointer<dreamscapes::VideoTimelineModel> timeline_;
    dreamscapes::vm::VideoDraft draft_;
    QString selectedPreset_ = QStringLiteral("Default"), presetFeedback_;
};
