#include "App/Views/Result/NativeReviewCompare.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;
        auto model=review::CompareModel::create();
        const auto entry=[](std::string source){return vm::Value(vm::Object{
            {"path",vm::Value(std::move(source))},{"mediaType",vm::Value("Image")}});};
        check(!model->open({entry("one.png")}) && !model->visible.get(),"one result entered comparison");
        good(model->open({entry("one.png"),entry("two.png")}));
        good(model->setViewport(800,600,8,22,140));
        auto rows=model->items.get();check(rows.size()==2 && model->visible.get() && !model->galleryVisible.get(),
                                           "two selected images did not enter comparison");
        auto wide=ii::gui::rgba_image(100,50,std::vector<std::uint8_t>(100*50*4,255));
        auto tall=ii::gui::rgba_image(50,100,std::vector<std::uint8_t>(50*100*4,255));
        check(bool(wide) && bool(tall),"comparison fixtures failed");
        check(model->publishImage(rows[0],*wide,{}) && model->publishImage(rows[1],*tall,{}),"comparison images were rejected");
        check(rows[0]->transform.get().scale==rows[1]->transform.get().scale && rows[0]->transform.get().fit,
              "fit did not use one synchronized pixel scale");
        good(model->actualSize());check(rows[0]->transform.get().scale==1 && rows[1]->transform.get().scale==1,
                                        "100 percent did not use available preview pixels");
        good(model->zoom(rows[0],2));good(model->pan(rows[0],10,15));
        check(rows[0]->transform.get()==rows[1]->transform.get() && rows[0]->transform.get().scale==2 &&
              rows[0]->transform.get().centerX!=.5,"synchronized zoom or pan diverged");
        good(model->setSynchronized(false));const auto unchanged=rows[0]->transform.get();
        good(model->zoom(rows[1],.5));check(rows[0]->transform.get()==unchanged && rows[1]->transform.get().scale==1,
                                            "independent zoom changed another pane");
        good(model->fit());check(rows[0]->transform.get()==unchanged && rows[1]->transform.get().fit,
                                 "independent fit changed another pane");
        good(model->actualSize());check(rows[0]->transform.get()==unchanged && rows[1]->transform.get().scale==1,
                                        "independent 100 percent changed another pane");
        good(model->setSynchronized(true));check(rows[0]->transform.get()==rows[1]->transform.get(),
                                                "enabling synchronization did not adopt the active pane");
        const auto retired=rows[0];model->close();
        check(!model->visible.get() && model->galleryVisible.get() && model->items.get().empty(),"closing did not restore the gallery");
        good(model->open({entry("three.png"),entry("four.png")}));
        check(!model->publishImage(retired,*wide,{}) && !model->items.get()[0]->image.get(),
              "a retired image callback updated a replacement comparison");
        model->close();
        auto closeOnOpen=model->visible.observe([&](bool value,std::uint64_t){if (value) model->close();});
        good(model->open({entry("five.png"),entry("six.png")}));
        check(!model->visible.get() && model->galleryVisible.get() && model->items.get().empty(),
              "reentrant closing left the gallery hidden");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
}
