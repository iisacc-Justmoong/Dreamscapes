#pragma once

#include "NativeHomeSelectedFile.h"
#include "HomeFileDetails.h"
#include <QObject>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace dreamscapes::home {
// Temporary Qt transport for existing Society values and the native view.
// This object is usable without a QML engine or any Qt view.
class NativeHomeSelectedFileSession final : public QObject {
  public:
    using Open = std::function<void(QVariantMap)>;
    NativeHomeSelectedFileSession(HomeSelectedFileStrings, Open, QObject *parent = nullptr);
    ~NativeHomeSelectedFileSession() override;
    [[nodiscard]] std::shared_ptr<HomeSelectedFileModel> model() const { return model_; }
    [[nodiscard]] ii::core::Result<void> setFile(QVariantMap);

  private:
    void metadataChanged();
    std::shared_ptr<HomeSelectedFileModel> model_;
    HomeFileDetails details_;
    QVariantMap file_;
    Open open_;
    std::uint64_t generation_ = 0;
    std::shared_ptr<bool> updating_ = std::make_shared<bool>(false);
};
} // namespace dreamscapes::home
