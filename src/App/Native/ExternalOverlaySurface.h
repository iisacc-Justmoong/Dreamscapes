#pragma once
#include "NativeOverlayCoordinator.h"
#include <QPointer>
#include <QWindow>
#include <functional>
#include <memory>

namespace ii::gui { class Host; }
namespace dreamscapes::native {
// Borrows the product window and owns an iiGUI surface until its deferred close.
// No SDK module depends on this product-level composition.
class ExternalOverlaySurface final : public NativeOverlaySurface,
                                     public std::enable_shared_from_this<ExternalOverlaySurface> {
public:
    using Predicate = std::function<bool()>;
    using Mount = std::function<bool(ii::gui::Host&)>;
    ExternalOverlaySurface(QWindow*,Predicate,Mount,std::function<void()> unmount);
    ~ExternalOverlaySurface() override;
    bool start(std::function<void(bool)> closed) override;
    void requestClose() noexcept override;
private:
    void mount();
    void retired();
    QPointer<QWindow> window_;
    Predicate wanted_;
    Mount mount_;
    std::function<void()> unmount_;
    std::unique_ptr<ii::gui::Host> host_;
    std::function<void(bool)> closed_;
    bool closing_=false,failed_=false;
};
}
