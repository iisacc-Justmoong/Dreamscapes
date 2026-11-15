#pragma once
#include "NativeApplicationShell.h"
#include <memory>

namespace dreamscapes::native {
[[nodiscard]] ii::core::Result<void> buildNativeApplicationShell(ii::gui::WidgetBuilder &,ii::gui::Widget,
    std::shared_ptr<NativeApplicationShell>);
}
class NativeApplicationShellBackend final {
  public:
    explicit NativeApplicationShellBackend(std::shared_ptr<dreamscapes::native::NativeApplicationShell> source)
        : model(std::move(source)),page(model->page),
          width(model->width),height(model->height),hasPageStatus(model->hasPageStatus),pageStatus(model->pageStatus) {}
    void sizeChanged(double w,double h) {model->sizeChanged(w,h);}
    void goHome() {(void)model->navigate("home");}
    std::shared_ptr<dreamscapes::native::NativeApplicationShell> model;
    ii::core::Observable<std::shared_ptr<ii::gui::NativeViewSource>> &page;
    ii::core::Observable<double> &width,&height;
    ii::core::Observable<bool> &hasPageStatus;
    ii::core::Observable<std::string> &pageStatus;
};
