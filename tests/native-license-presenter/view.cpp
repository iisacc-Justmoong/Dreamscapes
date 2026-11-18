#include "App/Native/NativeLicenseBridge.h"
#include "SocietyLicenseGate.generated.h"
#include <iiGUI>
#include <memory>
#include <stdexcept>

using ii::core::Result;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void good(Result<void> value){if(!value)throw std::runtime_error(value.error().message);}
template<class T>T take(Result<T> value){if(!value)throw std::runtime_error(value.error().message);return std::move(*value);}
struct Device final : ii::gui::PaintDevice {
    ii::gui::PaintCapabilities capabilities()const noexcept override{return {.api=ii::gui::GraphicsApi::vulkan,.hardware=true,.native_api=true,.backdrop=true,.shadow=true,.content_transform=true,.scene_sources=true};}
    Result<void> begin_frame(const ii::gui::PaintFrameInfo&)override{return {};}
    Result<void> begin_scene(const ii::gui::PaintSceneInfo&)override{return {};}
    Result<void> end_scene()override{return {};}
    Result<void> begin_layer(const ii::gui::PaintLayerInfo&)override{return {};}
    Result<void> draw_panels(std::span<const ii::gui::Panel>)override{return {};}
    Result<void> draw_text(std::span<const ii::gui::TextRun>)override{return {};}
    Result<void> draw_images(std::span<const ii::gui::ImageRun>)override{return {};}
    Result<void> end_layer()override{return {};}
    Result<void> present()override{return {};}
    void abort_frame()noexcept override{}
};
}
int main(){
    using View=lvui_generated::SocietyLicenseGate;
    using Model=iisacc::licensing::SocietyLicenseGateModel;
    std::shared_ptr<View::Scope> scope;
    ii::runtime::EventLoop loop;
    auto view=take(ii::gui::PaintView::create(std::make_shared<Device>(),[&](ii::gui::WidgetBuilder& builder){
        return View::build(builder,{}, {},&scope);
    },{},ii::gui::PaintViewServices{.events=&loop}));
    auto model=std::shared_ptr<Model>(scope,&scope->backend_0);
    iisacc::licensing::GateState state{.productName="Dreamscapes",.statusCode="license_not_owned"};
    int opens=0,retries=0;
    dreamscapes::native::NativeLicenseBridge bridge({[&]{return state;},[&]{++opens;return false;},[&]{++retries;}},"Open failed",model);
    good(view->paint({640,480}));
    auto status=take(view->find("licenseStatus"));
    auto open=take(view->find("openSocietyForLicense"));
    auto retry=take(view->find("checkSocietyLicense"));
    check(take(status.snapshot()).text.find("Dreamscapes")!=std::string::npos,"License state did not reach the compiled view");
    good(open.activate());check(opens==1&&take(status.snapshot()).text=="Open failed","The compiled button did not deliver the error");
    state.checking=true;bridge.refresh();check(!take(retry.snapshot()).enabled,"The compiled view enabled retry while checking");
    state.checking=false;bridge.refresh();good(retry.activate());check(retries==1,"The compiled retry did not reach the license source");
    dreamscapes::native::NativeLicenseBridge launchOnly({[&]{return state;},[]{return true;},{}},"Open failed",model);
    good(view->paint({640,480}));
    check(!take(retry.snapshot()).enabled,"The compiled launch-only view enabled retry");
    good(retry.activate());
    check(retries==1,
          "The compiled launch-only view allowed an additional license check");
    good(view->close());check(!open.snapshot(),"Closing the view retained its widgets");
}
