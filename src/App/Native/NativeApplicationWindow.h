#pragma once

#include <iiGUI>
#include <functional>
#include <memory>
#include <optional>

namespace ii::runtime { class Application; }
namespace dreamscapes::native {
// Owns an iiGUI top-level window, independent of QML and any borrowed window.
// All operations and callbacks run on the creating UI thread. The external
// event-loop path retains itself until iiGUI's deferred close callback.
class NativeApplicationWindow final : public std::enable_shared_from_this<NativeApplicationWindow> {
  public:
    struct Content final {
        std::function<ii::core::Result<void>(ii::gui::WidgetBuilder &)> build;
        std::shared_ptr<void> resources;
        std::function<ii::core::Result<void>(ii::gui::Size)> resize;
    };
    using Factory = std::function<ii::core::Result<Content>(ii::runtime::EventLoop &)>;
    struct Callbacks final {
        std::function<void()> ready;
        std::function<bool()> closeRequested;
        std::function<void(const ii::core::Error &)> failed;
        std::function<void()> closed;
    };

    static std::shared_ptr<NativeApplicationWindow> create(ii::gui::HostOptions, Factory, Callbacks = {});
    ~NativeApplicationWindow();
    NativeApplicationWindow(const NativeApplicationWindow &) = delete;
    NativeApplicationWindow &operator=(const NativeApplicationWindow &) = delete;

    // Own the native event loop until the window closes.
    [[nodiscard]] ii::core::Result<void> run();
    // Register the native driver and window content with Core's application.
    [[nodiscard]] ii::core::Result<void> attach(ii::runtime::Application &);
    // Nonblocking transition path for a running external OS event loop.
    [[nodiscard]] ii::core::Result<void> showExternal();
    // Requests a close; closeRequested may veto it without retiring the view.
    [[nodiscard]] ii::core::Result<void> close();
    [[nodiscard]] ii::core::Result<void> beginWindowDrag();
    [[nodiscard]] ii::core::Result<void> chooseFile(ii::gui::FileDialogOptions, ii::gui::FileDialogCompletion);
    [[nodiscard]] std::optional<ii::gui::WidgetMount> widgets() const;

  private:
    struct Impl;
    NativeApplicationWindow(ii::gui::HostOptions, Factory, Callbacks);
    ii::core::Result<void> prepare();
    void opened();
    void resized(ii::gui::Size);
    bool mayClose();
    void retired();
    void finish();
    void fail(ii::core::Error);
    std::unique_ptr<Impl> impl_;
};
} // namespace dreamscapes::native
