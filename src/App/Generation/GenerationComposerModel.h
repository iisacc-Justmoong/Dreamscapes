#pragma once
#include "../ViewModels/Native/GenerationDraft.h"
#include <QObject>
#include <QPointer>
#include <QVariant>
#include <QUrl>
class GenerationController;
class AdvancedImageParameters;
class HomeCanvas;
class GenerationComposerModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(GenerationComposerModel *sharedModel READ sharedModel WRITE setSharedModel NOTIFY bindingsChanged)
    Q_PROPERTY(QObject *generation READ generation WRITE setGeneration NOTIFY bindingsChanged)
    Q_PROPERTY(QObject *canvas READ canvas WRITE setCanvas NOTIFY bindingsChanged)
    Q_PROPERTY(QObject *parameterStore READ parameterStore WRITE setParameterStore NOTIFY bindingsChanged)
    Q_PROPERTY(QVariantList modelChoices READ modelChoices NOTIFY modelsChanged)
    Q_PROPERTY(QString selectedModel READ selectedModel NOTIFY modelsChanged)
    Q_PROPERTY(QString selectedModelName READ selectedModelName NOTIFY modelsChanged)
    Q_PROPERTY(QString prompt READ prompt WRITE setPrompt NOTIFY promptChanged)
    Q_PROPERTY(QString mediaType READ mediaType WRITE setMediaType NOTIFY mediaTypeChanged)
    Q_PROPERTY(QVariantMap defaultPrompts READ defaultPrompts WRITE setDefaultPrompts NOTIFY defaultPromptsChanged)
    Q_PROPERTY(QString appliedDefaultPrompt READ appliedDefaultPrompt WRITE setAppliedDefaultPrompt NOTIFY appliedDefaultPromptChanged)
    Q_PROPERTY(bool promptDefaultsReady READ promptDefaultsReady WRITE setPromptDefaultsReady NOTIFY promptDefaultsReadyChanged)
    Q_PROPERTY(QString aspectRatio READ aspectRatio WRITE setAspectRatio NOTIFY aspectRatioChanged)
    Q_PROPERTY(int generationCount READ generationCount WRITE setGenerationCount NOTIFY generationCountChanged)
    Q_PROPERTY(int videoDuration READ videoDuration WRITE setVideoDuration NOTIFY videoDurationChanged)
    Q_PROPERTY(int videoFps READ videoFps WRITE setVideoFps NOTIFY videoFpsChanged)
    Q_PROPERTY(int shortSide READ shortSide WRITE setShortSide NOTIFY shortSideChanged)
    Q_PROPERTY(double seed READ seed WRITE setSeed NOTIFY seedChanged)
    Q_PROPERTY(QString negativePrompt READ negativePrompt WRITE setNegativePrompt NOTIFY negativePromptChanged)
    Q_PROPERTY(QString appliedNegativePrompt READ appliedNegativePrompt WRITE setAppliedNegativePrompt NOTIFY appliedNegativePromptChanged)
    Q_PROPERTY(bool loadedPreset READ loadedPreset WRITE setLoadedPreset NOTIFY loadedPresetChanged)
    Q_PROPERTY(QString errorText READ errorText WRITE setErrorText NOTIFY errorTextChanged)
    Q_PROPERTY(QString selectedPresetName READ selectedPresetName WRITE setSelectedPresetName NOTIFY selectedPresetNameChanged)
    Q_PROPERTY(bool submitting READ submitting WRITE setSubmitting NOTIFY submittingChanged)
    Q_PROPERTY(bool generationPending READ generationPending WRITE setGenerationPending NOTIFY generationPendingChanged)
    Q_PROPERTY(bool video READ video NOTIFY videoChanged)
    Q_PROPERTY(QVariantMap outputSize READ outputSize NOTIFY outputSizeChanged)
    Q_PROPERTY(bool usesExplicitParameters READ usesExplicitParameters NOTIFY usesExplicitParametersChanged)
public:
    explicit GenerationComposerModel(QObject *parent = nullptr);
    GenerationComposerModel *sharedModel() const { return sharedModel_; }
    void setSharedModel(GenerationComposerModel *);
    QObject *generation() const; void setGeneration(QObject *);
    QObject *canvas() const; void setCanvas(QObject *);
    QObject *parameterStore() const; void setParameterStore(QObject *);
    QVariantList modelChoices() const;
    QString selectedModel() const;
    QString selectedModelName() const;
    QString prompt() const { return state_.value("prompt").toString(); }
    void setPrompt(QString value);
    QString mediaType() const { return state_.value("mediaType").toString(); }
    void setMediaType(QString value);
    QVariantMap defaultPrompts() const { return state_.value("defaultPrompts").toMap(); }
    void setDefaultPrompts(QVariantMap value);
    QString appliedDefaultPrompt() const { return state_.value("appliedDefaultPrompt").toString(); }
    void setAppliedDefaultPrompt(QString value);
    bool promptDefaultsReady() const { return state_.value("promptDefaultsReady").toBool(); }
    void setPromptDefaultsReady(bool value);
    QString aspectRatio() const { return state_.value("aspectRatio").toString(); }
    void setAspectRatio(QString value);
    int generationCount() const { return state_.value("generationCount").toInt(); }
    void setGenerationCount(int value);
    int videoDuration() const { return state_.value("videoDuration").toInt(); }
    void setVideoDuration(int value);
    int videoFps() const { return state_.value("videoFps").toInt(); }
    void setVideoFps(int value);
    int shortSide() const { return state_.value("shortSide").toInt(); }
    void setShortSide(int value);
    double seed() const { return state_.value("seed").toDouble(); }
    void setSeed(double value);
    QString negativePrompt() const { return state_.value("negativePrompt").toString(); }
    void setNegativePrompt(QString value);
    QString appliedNegativePrompt() const { return state_.value("appliedNegativePrompt").toString(); }
    void setAppliedNegativePrompt(QString value);
    bool loadedPreset() const { return state_.value("loadedPreset").toBool(); }
    void setLoadedPreset(bool value);
    QString errorText() const { return state_.value("errorText").toString(); }
    void setErrorText(QString value);
    QString selectedPresetName() const { return state_.value("selectedPresetName").toString(); }
    void setSelectedPresetName(QString value);
    bool submitting() const { return state_.value("submitting").toBool(); }
    void setSubmitting(bool value);
    bool generationPending() const { return state_.value("generationPending").toBool(); }
    void setGenerationPending(bool value);
    bool video() const { return state_.value("video").toBool(); }
    QVariantMap outputSize() const { return state_.value("outputSize").toMap(); }
    bool usesExplicitParameters() const { return state_.value("usesExplicitParameters").toBool(); }
    Q_INVOKABLE QVariantMap sizeFor(int shorter) const;
    Q_INVOKABLE void applyDefaultPrompt();
    Q_INVOKABLE void applyNegativeDefault();
    Q_INVOKABLE bool addAttachment(const QUrl &);
    Q_INVOKABLE QVariantMap generationParameters(const QString &model);
    Q_INVOKABLE QVariantMap parameters(const QString &model, bool includeInputs = true);
    Q_INVOKABLE bool savePreset(const QString &);
    Q_INVOKABLE bool loadPreset(const QString &);
    Q_INVOKABLE void chooseMedia(const QString &);
    Q_INVOKABLE void submit();
signals:
    void snapshotChanged(QVariantMap snapshot);
    void bindingsChanged();
    void modelsChanged();
    void promptRequested(QString prompt);
    void focusPromptRequested();
    void dismissInputRequested();
    void generateRequested(QString prompt, QString mediaType, QString ratio, int count);
    void mediaActionRequested(QString action);
    void promptChanged();
    void mediaTypeChanged();
    void defaultPromptsChanged();
    void appliedDefaultPromptChanged();
    void promptDefaultsReadyChanged();
    void aspectRatioChanged();
    void generationCountChanged();
    void videoDurationChanged();
    void videoFpsChanged();
    void shortSideChanged();
    void seedChanged();
    void negativePromptChanged();
    void appliedNegativePromptChanged();
    void loadedPresetChanged();
    void errorTextChanged();
    void selectedPresetNameChanged();
    void submittingChanged();
    void generationPendingChanged();
    void videoChanged();
    void outputSizeChanged();
    void usesExplicitParametersChanged();
private:
    void edit(std::string key, const QVariant &);
    void acceptSnapshot(const QVariantMap &);
    QPointer<GenerationComposerModel> sharedModel_;
    dreamscapes::vm::ComposerState model_;
    QVariantMap state_;
    QPointer<GenerationController> generation_;
    QPointer<HomeCanvas> canvas_;
    QPointer<AdvancedImageParameters> parameterStore_;
};
