#include "App/Views/Editor/NativeTextTool.h"
#include "App/Views/Editor/NativeEditorWorkspace.h"
#include "App/Native/NativeCanvasFiles.h"
#include "NativeAssets.h"
#include <Document/Document.h>
#include <Render/FrameRenderer.h>
#include <Validation/Validation.h>
#include <iiFileProvider>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}

int main() {
    try {
        using namespace dreamscapes;using namespace iiSharedCanvas;
        const auto defaults=editor::parseTextDefaults(native::toolCatalog);
        check(defaults && defaults->size==13 && defaults->family=="Pretendard" && defaults->color==0xffffffff,
              "text defaults were not loaded from the existing tool catalog");
        check(!editor::parseTextDefaults("[]"),"a missing text catalog was accepted");
        auto document=native::DocumentSession::create();Document canvas;canvas.extent={100,50};
        good(document->attach(canvas,"Text.iisc"));
        auto workspace=editor::WorkspaceModel::create(document);
        auto tool=workspace->textTool();
        editor::TextOutlineRequest captured;int calls=0;
        tool->setOutliner([&](const editor::TextOutlineRequest &request) -> ii::core::Result<std::vector<VectorPath>> {
            captured=request;++calls;VectorPath path;path.fill=SolidPaint{request.color};
            path.commands={MoveTo{request.origin},LineTo{{request.origin.x+20,request.origin.y}},
                LineTo{{request.origin.x+20,request.origin.y+10}},LineTo{{request.origin.x,request.origin.y+10}},ClosePath{}};
            return std::vector<VectorPath>{std::move(path)};
        });
        good(tool->setContent("Hello 한글"));good(tool->setFontSize(20));good(tool->setColor("#123456"));
        check(!tool->setFontSize(0) && !tool->setColor("not a color"),"invalid typography was accepted");
        auto image=ii::gui::rgba_image(100,50,std::vector<std::uint8_t>(100*50*4,255));
        check(bool(image),"text preview fixture failed");
        workspace->setVisible(true);workspace->showTextPanel();
        tool->setViewport(200,200);tool->setFrame(*image);
        auto send=[&](ii::gui::WidgetInputKind kind,double x,double y,std::uint64_t device=1) {
            ii::gui::WidgetInput input;input.kind=kind;input.device_id=device;input.local_position=ii::gui::WidgetPoint{x,y};
            auto result=tool->input(input);if (!result) throw std::runtime_error(result.error().message);return *result;
        };
        check(workspace->textPanelActive.get() && !send(ii::gui::WidgetInputKind::pointer_down,10,10),
              "text tool was not active or accepted the letterbox");
        const auto original=document->snapshot();const auto revision=document->revision.get();
        check(send(ii::gui::WidgetInputKind::pointer_down,50,75) && calls==0 && document->snapshot()==original,
              "text pointer press edited the canvas");
        check(send(ii::gui::WidgetInputKind::pointer_up,50,75) && calls==1 && document->dirty.get() &&
              document->revision.get()==revision+1 && original->layers.empty(),"text did not create a vector layer");
        check(captured.content=="Hello 한글" && captured.size==20 && captured.color==0xff123456 &&
              std::abs(captured.origin.x-25)<1e-6 && std::abs(captured.origin.y-12.5)<1e-6,
              "text settings or canvas coordinates were not forwarded");
        const auto &layer=std::get<StaticVectorLayer>(document->document()->layers.front());
        check(document->selectedLayer.get()==layer.properties.id && renderFrame(*document->document(),0).ok(),
              "the generated text layer was not selected or renderable");
        iiFileProvider::native::TemporaryDirectory temporary(std::filesystem::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temporary.isValid(),"cannot create the text file fixture");
        check(bool(native::createCanvasFile(temporary.path()/"text.iisc",*document->document())),"cannot save text canvas");
        const auto reopened=native::openCanvasFile(temporary.path()/"text.iisc");
        check(reopened && reopened->document.layers.size()==1 && renderFrame(reopened->document,0).ok(),
              "text vector did not survive IISC save and reopen");
        check(send(ii::gui::WidgetInputKind::pointer_down,50,75),"second text gesture did not start");
        Document replacement;replacement.extent={100,50};good(document->attach(replacement,"Other.iisc"));
        check(!send(ii::gui::WidgetInputKind::pointer_up,50,75) && calls==1 && document->document()->layers.empty(),
              "a stale text gesture modified a replacement canvas");
        Document animated;animated.extent={100,50};animated.timeline.frameCount=2;
        good(document->attach(animated,"Animated.iisc"));tool->setFrame(*image);
        check(send(ii::gui::WidgetInputKind::pointer_down,50,75),"text placement did not start on the animated canvas");
        good(document->seek(1));
        check(!send(ii::gui::WidgetInputKind::pointer_up,50,75) && calls==1 && document->document()->layers.empty(),
              "changing frames did not cancel a pending text placement");
        if (editor::nativeTextOutlineAvailable()) {
            editor::TextOutlineRequest request{"Hello 한글","Pretendard",20,0xffffffff,{4,4},190,90};
            auto paths=editor::nativeTextOutline(request);
            check(paths && !paths->empty() && !paths->front().commands.empty(),"native shaped text did not produce vector paths");
            auto actual=native::DocumentSession::create();Document blank;blank.extent={200,100};good(actual->attach(blank,"Native.iisc"));
            const auto before=renderFrame(*actual->document(),0);
            check(bool(actual->insertVectorDrawing("Native text",std::move(*paths))),"native outlines could not be inserted");
            const auto after=renderFrame(*actual->document(),0);
            check(before.ok() && after.ok() && before.pixels.pixels!=after.pixels.pixels,
                  "native text outlines did not change the rendered canvas");
            check(bool(native::createCanvasFile(temporary.path()/"native-text.iisc",*actual->document())),
                  "native text canvas could not be saved");
            auto reopenedNative=native::openCanvasFile(temporary.path()/"native-text.iisc");
            check(reopenedNative && renderFrame(reopenedNative->document,0).pixels.pixels==after.pixels.pixels,
                  "native text appearance did not survive save and reopen");
        }
    } catch (const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
}
