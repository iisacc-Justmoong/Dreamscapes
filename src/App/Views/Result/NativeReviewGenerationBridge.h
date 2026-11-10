#pragma once

#include "NativeReviewGallery.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariant>
#include <functional>
#include <memory>

namespace dreamscapes {
class GenerationProjection;
namespace review {

// Transitional transport between the existing Qt generation projection and
// the Qt-free review model. The application supplies its real action handlers.
struct NativeReviewGenerationActions final {
    using Action = std::function<void(const QVariantList &)>;
    Action edit, compare, exportResults, detail;
};

class NativeReviewGenerationBridge final : public QObject {
  public:
    NativeReviewGenerationBridge(GenerationProjection *, std::shared_ptr<ReviewGalleryModel>,
                                 NativeReviewGenerationActions = {}, QObject *parent = nullptr);
    ~NativeReviewGenerationBridge() override;
    void detach();

  private:
    using Action = NativeReviewGenerationActions::Action;
    void synchronize();
    void deliver(const Action &, const std::vector<vm::Value> &);

    QPointer<GenerationProjection> projection_;
    std::weak_ptr<ReviewGalleryModel> model_;
    NativeReviewGenerationActions actions_;
    QHash<QString, QVariant> originals_;
    bool attached_ = false;
};
} // namespace review
} // namespace dreamscapes
