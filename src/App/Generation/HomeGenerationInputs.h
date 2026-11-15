#pragma once

#include <QString>
#include <QUrl>
#include <QVariant>

namespace dreamscapes {
// Input ownership and image preparation belong to the provider, not to a view.
// The QObject passed to GenerationComposerModel owns this interface's lifetime.
class HomeGenerationInputs {
  public:
    virtual ~HomeGenerationInputs() = default;
    [[nodiscard]] virtual bool hasContent() const = 0;
    [[nodiscard]] virtual QVariantList attachments() const = 0;
    [[nodiscard]] virtual QString inputError() const = 0;
    virtual bool addAttachment(const QUrl &) = 0;
    [[nodiscard]] virtual QVariantMap generationParameters(const QString &prompt, const QString &model,
                                                            const QString &ratio, int count) = 0;
};
} // namespace dreamscapes
