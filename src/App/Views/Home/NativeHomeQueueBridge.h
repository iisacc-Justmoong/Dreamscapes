#pragma once

#include "NativeHomeQueue.h"
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <memory>

namespace dreamscapes {
class GenerationProjection;
namespace home {
// Temporary Qt transport; the LVUI view and model remain Qt-free.
class NativeHomeQueueBridge final : public QObject {
  public:
    NativeHomeQueueBridge(GenerationProjection *projection, std::shared_ptr<HomeQueueModel> model,
                          std::function<void()> openQueue, QObject *parent = nullptr);
    ~NativeHomeQueueBridge() override;
    void setRuntimeNames(QString model, QString device);
    void detach();

  private:
    void synchronize();
    QPointer<GenerationProjection> projection_;
    std::weak_ptr<HomeQueueModel> model_;
    std::function<void()> openQueue_;
    QString modelName_, deviceName_{QStringLiteral("Automatic")};
    bool attached_ = false;
};
} // namespace home
} // namespace dreamscapes
