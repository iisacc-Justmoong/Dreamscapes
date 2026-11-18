#include "NativeTitleBar.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace dreamscapes::native {
std::shared_ptr<NativeTitleBarModel> NativeTitleBarModel::create(bool nativeControls,Drag drag,
                                                                std::function<void()> account) {
    auto result=std::make_shared<NativeTitleBarModel>();
    result->setWindowFeatures(nativeControls,std::move(drag));
    result->account_=std::move(account);
    return result;
}
void NativeTitleBarModel::setWindowFeatures(bool controls,Drag drag) {
    nativeControls=controls;
    drag_=std::move(drag);
}
void NativeTitleBarModel::sizeChanged(double value,double preferred,double inset,double trailing) {
    if (!std::isfinite(value) || !std::isfinite(preferred) || !std::isfinite(inset) ||
        !std::isfinite(trailing) || value<0 || preferred<0 || inset<0 || trailing<0) return;
    const double leading=nativeControls?inset:0;
    (void)width.set(value);
    (void)leadingWidth.set(leading);
    // LVUI input fields require a positive width even before the first layout.
    (void)agentWidth.set(std::clamp(value-leading-trailing,1.0,std::max(1.0,preferred)));
}
void NativeTitleBarModel::editAgent(const std::string &text) {(void)agentText.set(text);}
ii::core::Result<bool> NativeTitleBarModel::drag(const ii::gui::WidgetInput &input) {
    if (input.kind!=ii::gui::WidgetInputKind::pointer_down || (input.buttons && !(input.buttons&1)) || !drag_)
        return false;
    if (auto result=drag_(); !result) return std::unexpected(result.error());
    return true;
}
void NativeTitleBarModel::account() {if (account_) account_();}
}
