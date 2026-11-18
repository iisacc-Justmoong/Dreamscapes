#pragma once

#include <iiGUI>
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ii::runtime { class EventLoop; class Runtime; }
namespace dreamscapes::native {
// A temporary platform surface for a caller-owned LVUI view. The content
// factory and its retained resources are independent of the window host.
class NativeEmbeddedSurface final : public std::enable_shared_from_this<NativeEmbeddedSurface> {
  public:
    struct Content final {
        std::function<ii::core::Result<void>(ii::gui::WidgetBuilder &)> build;
        std::shared_ptr<void> resources;
    };
    struct Options final { std::size_t workers = 0, capacity = 16; };
    struct Callbacks final {
        std::function<void()> ready;
        std::function<void(const std::string &)> failed;
        std::function<void()> closed;
    };
    using Factory = std::function<ii::core::Result<Content>(ii::runtime::EventLoop &, ii::runtime::Runtime *)>;
    static std::shared_ptr<NativeEmbeddedSurface> create(void *nativeParent, Factory, Options, Callbacks = {});
    ~NativeEmbeddedSurface();
    NativeEmbeddedSurface(const NativeEmbeddedSurface &) = delete;
    NativeEmbeddedSurface &operator=(const NativeEmbeddedSurface &) = delete;
    [[nodiscard]] ii::core::Result<void> start(ii::gui::Rect);
    [[nodiscard]] ii::core::Result<void> setFrame(ii::gui::Rect);
    [[nodiscard]] ii::core::Result<void> setVisible(bool);
    [[nodiscard]] ii::core::Result<void> close();
    std::optional<ii::gui::WidgetMount> widgets() const;
  private:
    struct Impl;
    NativeEmbeddedSurface(void *, Factory, Options, Callbacks);
    void opened();
    void hostClosed();
    void drainWorkers();
    void workersDrained(bool);
    void requestHostClose();
    void finish();
    void fail(std::string);
    std::unique_ptr<Impl> impl_;
};
} // namespace dreamscapes::native
