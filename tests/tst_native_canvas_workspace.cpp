#include "App/Views/Home/NativeCanvasWorkspace.h"
#include "App/Native/NativeDocumentSession.h"
#include "NativeAssets.h"
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;
        auto model=canvas::WorkspaceModel::create(native::canvasCatalog);
        check(!model->categories.get().empty() && !model->presets.get().empty(),"original canvas catalogue was not loaded");
        good(model->search("X"));
        check(!model->presets.get().empty(),"search did not preserve the X platform name");
        good(model->customSize());
        const auto listRevision=model->presets.revision();
        good(model->edit("draftWidth","800")); good(model->edit("draftHeight","600"));
        check(model->presets.revision()==listRevision,"editing dimensions rebuilt the unchanged preset list");
        check(model->valid.get(),"valid canvas dimensions were rejected");
        good(model->edit("draftWidth","-1"));
        check(!model->valid.get(),"negative canvas dimension was accepted");
        good(model->edit("draftWidth","800"));
        good(model->changeUnit("in")); good(model->changeUnit("px"));
        check(model->widthText.get()=="800" && model->heightText.get()=="600",
              "canvas unit conversion did not preserve pixel dimensions");
        int calls=0;
        model->setCreateHandler([&](vm::Value specification)->ii::core::Result<void> {
            ++calls;
            check(vm::number(vm::get(specification,"pixelWidth"))==800 &&
                  vm::number(vm::get(specification,"pixelHeight"))==600,"incorrect canvas dimensions");
            return {};
        });
        good(model->createCanvas()); check(calls==1,"valid canvas was not delivered");
        good(model->edit("draftHeight","invalid")); check(!model->createCanvas() && calls==1,"invalid canvas reached the handler");
        auto document=native::DocumentSession::create();
        iiSharedCanvas::Document value; value.extent={8,6};
        good(document->attach(value,"New",true));
        check(document->dirty.get(),"new unsaved document was treated as persisted");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
