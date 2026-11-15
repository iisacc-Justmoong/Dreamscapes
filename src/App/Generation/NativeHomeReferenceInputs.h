#pragma once

#include "HomeGenerationInputs.h"
#include "HomeReferenceStore.h"
#include <QObject>

namespace dreamscapes {
// Reference inputs for the native Home, without a Qt Quick canvas or view.
class NativeHomeReferenceInputs final : public QObject, public HomeGenerationInputs {
    Q_OBJECT
  public:
    explicit NativeHomeReferenceInputs(QObject *parent = nullptr) : QObject(parent) {}
    [[nodiscard]] bool hasContent() const override { return false; }
    [[nodiscard]] QVariantList attachments() const override { return store_.attachments(); }
    [[nodiscard]] QString inputError() const override { return error_; }
    bool addAttachment(const QUrl &) override;
    bool removeAttachment(const QString &key);
    [[nodiscard]] QVariantMap generationParameters(const QString &prompt, const QString &model,
                                                    const QString &ratio, int count) override;
  signals:
    void attachmentsChanged();
    void inputErrorChanged();
  private:
    void setError(QString);
    HomeReferenceStore store_;
    QString error_;
};
} // namespace dreamscapes
