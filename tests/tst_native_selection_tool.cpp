#include "App/Views/Editor/NativeSelectionTool.h"
#include "App/Views/Editor/NativeElementsPanel.h"
#include <Document/Document.h>
#include <iiGUI>
#include <cmath>
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
 try {
    using namespace dreamscapes;using namespace iiSharedCanvas;
    auto session=native::DocumentSession::create();Document doc;doc.extent={100,50};good(session->attach(doc,"Select.iisc"));
    VectorPath rectangle;rectangle.fill=SolidPaint{0xff123456};
    rectangle.commands={MoveTo{{20,10}},LineTo{{60,10}},LineTo{{60,30}},LineTo{{20,30}},ClosePath{}};
    const auto id=session->insertVectorDrawing("Rectangle",{rectangle});check(bool(id),"shape fixture failed");
    auto panel=editor::ElementsPanelModel::create(session);panel->setVisible(true);
    auto tool=editor::SelectionToolModel::create(session);auto frame=ii::gui::rgba_image(100,50,std::vector<std::uint8_t>(100*50*4,255));
    check(bool(frame),"frame fixture failed");tool->setViewport(200,200,8);tool->setFrame(*frame);tool->setVisible(true);
    auto near=[](double a,double b){return std::abs(a-b)<.001;};
    auto overlay=tool->overlay.get();
    check(overlay.active && near(overlay.corners[0].x,40) && near(overlay.corners[0].y,70) &&
          near(overlay.corners[2].x,120) && near(overlay.corners[2].y,110),"selection bounds did not follow vector geometry");
    auto send=[&](ii::gui::WidgetInputKind kind,double x,double y,std::uint64_t device=1) {
        ii::gui::WidgetInput event;event.kind=kind;event.device_id=device;event.local_position=ii::gui::WidgetPoint{x,y};
        auto result=tool->input(event);if (!result) throw std::runtime_error(result.error().message);return *result;
    };
    const auto revision=session->revision.get();const auto old=layerProperties(*findLayer(*session->document(),*id)).transform;
    check(send(ii::gui::WidgetInputKind::pointer_down,80,90),"selected shape did not accept move");
    check(send(ii::gui::WidgetInputKind::pointer_move,100,100) && tool->dragging.get() &&
          session->revision.get()==revision && near(tool->overlay.get().corners[0].x,60),"moving preview changed the document");
    check(send(ii::gui::WidgetInputKind::pointer_up,100,100),"move release was not handled");
    const auto moved=layerProperties(*findLayer(*session->document(),*id)).transform;
    check(session->revision.get()==revision+1 && near(moved.translationX,old.translationX+10) &&
          near(moved.translationY,old.translationY+5),"move did not commit exactly one transform");
    const auto beforeResize=session->revision.get();
    check(send(ii::gui::WidgetInputKind::pointer_down,140,120),"resize handle did not accept input");
    check(send(ii::gui::WidgetInputKind::pointer_move,180,140) && session->revision.get()==beforeResize,
          "resizing preview changed the document");
    check(send(ii::gui::WidgetInputKind::pointer_up,180,140),"resize release was not handled");
    check(session->revision.get()==beforeResize+1 && near(panel->elementWidth.get(),60) && near(panel->elementHeight.get(),30),
          "resizing did not update layer geometry and inspector values");
    const auto committed=session->revision.get();check(send(ii::gui::WidgetInputKind::pointer_down,100,100),"cancel fixture did not start");
    check(send(ii::gui::WidgetInputKind::pointer_move,110,110),"cancel preview did not move");tool->setVisible(false);
    check(!tool->dragging.get() && session->revision.get()==committed,"hiding the tool committed a preview");
    tool->setVisible(true);VectorPath other;other.fill=SolidPaint{0xff123456};
    other.commands={MoveTo{{2,2}},LineTo{{12,2}},LineTo{{12,8}},LineTo{{2,8}},ClosePath{}};
    const auto otherId=session->insertVectorDrawing("Other",{other});check(bool(otherId),"second shape fixture failed");
    good(session->selectLayer(*id));
    check(send(ii::gui::WidgetInputKind::pointer_down,12,60),"canvas hit did not select another vector");
    check(send(ii::gui::WidgetInputKind::pointer_up,12,60) && session->selectedLayer.get()==*otherId,
          "canvas selection chose the wrong layer");
    const auto beforeClear=session->revision.get();
    check(send(ii::gui::WidgetInputKind::pointer_down,190,10) && !session->hasCurrentLayerSelection() &&
          !tool->overlay.get().active && session->revision.get()==beforeClear,
          "clicking empty space did not clear selection without editing the document");
    check(send(ii::gui::WidgetInputKind::pointer_down,12,60) && send(ii::gui::WidgetInputKind::pointer_up,12,60) &&
          session->selectedLayer.get()==*otherId,"a shape could not be selected after clearing selection");
    const auto beforeReplace=session->revision.get();check(send(ii::gui::WidgetInputKind::pointer_down,12,60),"replacement fixture did not start");
    Document replacement;replacement.extent={100,50};good(session->attach(replacement,"Other.iisc"));
    check(!tool->dragging.get() && !tool->overlay.get().active && session->revision.get()!=beforeReplace,
          "replacing the document retained a selection preview");
    auto rotatedSession=native::DocumentSession::create();Document rotatedDoc;rotatedDoc.extent={100,50};
    good(rotatedSession->attach(rotatedDoc,"Rotated.iisc"));
    const auto rotatedId=rotatedSession->insertVectorDrawing("Rotated",{rectangle});check(bool(rotatedId),"rotated shape fixture failed");
    auto transform=layerProperties(*findLayer(*rotatedSession->document(),*rotatedId)).transform;
    transform.m11=0;transform.m12=1;transform.m21=-1;transform.m22=0;transform.translationX=80;transform.translationY=0;
    good(rotatedSession->setLayerTransform(*rotatedId,transform));
    auto rotatedTool=editor::SelectionToolModel::create(rotatedSession);
    auto rotatedPanel=editor::ElementsPanelModel::create(rotatedSession);rotatedPanel->setVisible(true);
    rotatedTool->setViewport(200,200,8);rotatedTool->setFrame(*frame);rotatedTool->setVisible(true);
    const auto before=rotatedTool->overlay.get();check(before.active,"rotated vector had no handles");
    const auto sendRotated=[&](ii::gui::WidgetInputKind kind,double x,double y) {
        ii::gui::WidgetInput event;event.kind=kind;event.local_position=ii::gui::WidgetPoint{x,y};
        auto result=rotatedTool->input(event);if (!result) throw std::runtime_error(result.error().message);return *result;
    };
    const auto corner=before.corners[2];check(sendRotated(ii::gui::WidgetInputKind::pointer_down,corner.x,corner.y),"rotated handle did not start");
    check(sendRotated(ii::gui::WidgetInputKind::pointer_move,corner.x,corner.y+40) &&
          sendRotated(ii::gui::WidgetInputKind::pointer_up,corner.x,corner.y+40),"rotated resize did not finish");
    const auto after=rotatedTool->overlay.get();
    check(near(rotatedPanel->elementWidth.get(),60) && near(rotatedPanel->elementHeight.get(),20) &&
          near(before.corners[0].x,after.corners[0].x) && near(before.corners[0].y,after.corners[0].y),
          "rotated resizing moved its opposite anchor or used screen axes");
    const auto safeRevision=rotatedSession->revision.get();
    auto hide=rotatedTool->overlay.observe([&](const auto &value,std::uint64_t){if (value.active && rotatedTool->dragging.get()) rotatedTool->setVisible(false);});
    const auto center=iiSharedCanvas::Point{(after.corners[0].x+after.corners[2].x)/2,(after.corners[0].y+after.corners[2].y)/2};
    check(sendRotated(ii::gui::WidgetInputKind::pointer_down,center.x,center.y) &&
          sendRotated(ii::gui::WidgetInputKind::pointer_move,center.x+2,center.y+2) &&
          !rotatedTool->dragging.get() && rotatedSession->revision.get()==safeRevision,
          "reentrant hiding retained or committed a transform gesture");
    auto clipped=*rotatedSession->document();clipped.artboards.push_back({"board","Board",{{0,0},{50,50}}});
    layerProperties(*findLayer(clipped,*rotatedId)).artboardId="board";
    good(rotatedSession->attach(std::move(clipped),"Artboard.iisc"));good(rotatedSession->selectLayer(*rotatedId));
    rotatedTool->setFrame(*frame);rotatedTool->setVisible(true);
    check(!rotatedTool->overlay.get().active,"an artboard-clipped layer exposed an incorrect unrestricted transform");
 } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
