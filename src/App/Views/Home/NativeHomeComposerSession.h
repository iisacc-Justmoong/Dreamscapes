#pragma once
#include "NativeHomeComposer.h"
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <optional>

class GenerationComposerModel;
class AdvancedImageParameters;

namespace dreamscapes::home {
// Transport for existing non-view Qt controllers. It does not need a QML engine.
class NativeHomeComposerSession final : public QObject {
  public:
    struct Callbacks final {
        std::function<ii::core::Result<void>()> chooseAttachments, paint;
        std::function<ii::core::Result<void>(std::string)> removeAttachment;
        std::function<ii::core::Result<void>(QVariantMap)> advanced;
        std::function<void(ii::core::Error)> failure;
    };
    NativeHomeComposerSession(GenerationComposerModel *, HomeComposerChoices,
                              std::string randomSeedLabel, Callbacks, QObject *parent = nullptr);
    ~NativeHomeComposerSession() override;
    [[nodiscard]] std::shared_ptr<HomeComposerModel> model() const { return model_; }
    [[nodiscard]] bool isFor(const GenerationComposerModel *) const;
    [[nodiscard]] ii::core::Result<void> setChoices(HomeComposerChoices);
    [[nodiscard]] ii::core::Result<void> setAttachments(std::vector<HomeComposerAttachment>);
    [[nodiscard]] ii::core::Result<void> setInputState(std::vector<HomeComposerAttachment>, std::string error);
    [[nodiscard]] ii::core::Result<void> synchronize();
  private:
    [[nodiscard]] ii::core::Result<void> command(const HomeComposerCommand &);
    void refresh();
    void bindParameterStore();
    std::shared_ptr<HomeComposerModel> model_;
    QPointer<GenerationComposerModel> composer_;
    QPointer<AdvancedImageParameters> parameterStore_;
    QPointer<QObject> generation_;
    QMetaObject::Connection parameterChanged_, parameterDestroyed_, generationDestroyed_;
    HomeComposerChoices choices_;
    std::vector<HomeComposerChoice> modelChoices_, presetChoices_;
    std::vector<HomeComposerAttachment> attachments_;
    std::string randomSeedLabel_;
    std::string inputError_;
    Callbacks callbacks_;
    bool modelsDirty_ = true, presetsDirty_ = true;
};
} // namespace dreamscapes::home
