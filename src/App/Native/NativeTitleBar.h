#pragma once
#include <iiGUI>
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <string>

namespace dreamscapes::native {
// Per-window presentation state. Native actions are injected by the window
// owner so the view never owns a host or an application service.
class NativeTitleBarModel final {
  public:
    using Drag = std::function<ii::core::Result<void>()>;
    static std::shared_ptr<NativeTitleBarModel> create(bool nativeControls, Drag drag,
                                                       std::function<void()> account);
    void setWindowFeatures(bool nativeControls, Drag drag);
    void sizeChanged(double width, double preferredInput, double nativeInset, double trailingWidth);
    void editAgent(const std::string &text);
    [[nodiscard]] ii::core::Result<bool> drag(const ii::gui::WidgetInput &input);
    void account();

    bool nativeControls=false;
    ii::core::Observable<double> width{1}, leadingWidth{0}, agentWidth{1};
    ii::core::Observable<std::string> agentText;
  private:
    Drag drag_;
    std::function<void()> account_;
};
}
