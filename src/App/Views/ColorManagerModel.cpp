#include "ColorManagerModel.h"
#include "../ViewModels/Native/ViewValue.h"
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QQuickWindow>
#include <QtQml/qqml.h>
#include <algorithm>
#include <cmath>

namespace CM=iiColorManager;
namespace {
CM::Rgba nativeColor(const QColor& c) { return {c.redF(),c.greenF(),c.blueF(),c.alphaF()}; }
QColor uiColor(CM::Rgba c) { return QColor::fromRgbF(c.r,c.g,c.b,c.a); }
QString text(std::string_view s) { return QString::fromUtf8(s.data(),qsizetype(s.size())); }
}
QColor ColorManagerModel::color() const { return uiColor(picker_.color()); }
QColor ColorManagerModel::previousColor() const { return uiColor(picker_.previousColor()); }
QString ColorManagerModel::hex() const { return QString::fromStdString(CM::toHex(picker_.color())).mid(1); }
QString ColorManagerModel::modeName() const { return text(CM::modeInfo(picker_.mode())->name); }
QStringList ColorManagerModel::modeNames() const { QStringList out;for(const auto& m:CM::modes())out.push_back(text(m.name));return out; }
QVariantList ColorManagerModel::channels() const { QVariantList out;for(const auto c:CM::modeInfo(picker_.mode())->channels)out.push_back(int(c));return out; }
void ColorManagerModel::changed(bool userEdit) { ++revision_;emit stateChanged();if(userEdit)emit colorEdited(color()); }
void ColorManagerModel::setMode(int value) {
    if(value==mode()||!picker_.setMode(CM::Mode(value)))return;
    ++revision_;emit modeChanged();emit stateChanged();
}
void ColorManagerModel::beginColor(const QColor& value) { if(value.isValid()&&picker_.begin(nativeColor(value)))changed(false); }
bool ColorManagerModel::editColor(const QColor& value) { if(!value.isValid()||!picker_.editColor(nativeColor(value)))return false;changed(true);return true; }
bool ColorManagerModel::editHex(const QString& value) { if(!picker_.editHex(value.toStdString()))return false;changed(true);return true; }
bool ColorManagerModel::editChannel(int c,double value) { if(!picker_.edit(CM::Channel(c),value))return false;changed(true);return true; }
bool ColorManagerModel::editChannelText(int channel, const QString &input) {
    const auto info=CM::channelInfo(CM::Channel(channel));if(!info)return false;
    const dreamscapes::vm::Value field(dreamscapes::vm::Object{{"unit",dreamscapes::vm::Value(std::string(info->unit))}});
    const auto parsed=dreamscapes::vm::parseNumber(field,input.toStdString(),true);
    return parsed.valid && editChannel(channel,dreamscapes::vm::number(parsed.value));
}
double ColorManagerModel::channelValue(int c) const { return picker_.channelValue(CM::Channel(c)); }
QVariantMap ColorManagerModel::channel(int c) const {
    const auto i=CM::channelInfo(CM::Channel(c));if(!i)return {};
    return {{"label",text(i->label)},{"name",text(i->name)},{"unit",text(i->unit)},
        {"minimum",i->minimum},{"maximum",i->maximum},{"step",i->step},{"value",picker_.channelValue(i->channel)}};
}
void ColorManagerModel::accept() { picker_.accept();changed(false); }
void ColorManagerModel::cancel() { picker_.cancel();changed(false); }
bool ColorManagerModel::snapWebSafe() { return editColor(uiColor(CM::nearestWebSafe(picker_.color()))); }
bool ColorManagerModel::editSurface(int field,double x,double y,int c) { if(!picker_.editSurface(CM::Surface(field),x,y,CM::Channel(c)))return false;changed(true);return true; }

ColorManagerSurface::ColorManagerSurface(QQuickItem* parent):QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);setActiveFocusOnTab(true);setAntialiasing(true);
}
void ColorManagerSurface::setModel(ColorManagerModel* model) {
    if(model_==model)return;if(model_)disconnect(model_,nullptr,this,nullptr);model_=model;
    if(model_)connect(model_,&ColorManagerModel::stateChanged,this,[this]{update();});
    emit modelChanged();update();
}
void ColorManagerSurface::setField(int field) { if(field<0||field>ChannelRamp||field_==field)return;field_=field;emit fieldChanged();update(); }
void ColorManagerSurface::setChannel(int c) { if(!CM::channelInfo(CM::Channel(c))||channel_==c)return;channel_=c;emit channelChanged();update(); }
void ColorManagerSurface::setInteractive(bool value) { if(interactive_==value)return;interactive_=value;setAcceptedMouseButtons(value?Qt::LeftButton:Qt::NoButton);setActiveFocusOnTab(value);emit interactiveChanged(); }
void ColorManagerSurface::setShowMarker(bool value) { if(showMarker_==value)return;showMarker_=value;emit showMarkerChanged();update(); }
void ColorManagerSurface::setCheckerboard(bool value) { if(checkerboard_==value)return;checkerboard_=value;emit checkerboardChanged();update(); }
void ColorManagerSurface::paint(QPainter* p) {
    if(!model_||width()<=0||height()<=0)return;
    const double scale=window()?window()->devicePixelRatio():1.;
    const auto w=std::uint32_t(std::clamp(std::ceil(width()*scale),1.,4096.));
    const auto h=std::uint32_t(std::clamp(std::ceil(height()*scale),1.,4096.));
    const auto raster=CM::renderSurface(model_->picker(),CM::Surface(field_),w,h,CM::Channel(channel_));if(!raster)return;
    if(checkerboard_&&field_==ChannelRamp&&channel_==int(CM::Channel::Alpha))for(int y=0;y<int(height());y+=6)for(int x=0;x<int(width());x+=6)p->fillRect(x,y,6,6,((x/6+y/6)&1)?QColor("#707070"):QColor("#B0B0B0"));
    const QImage image(raster->rgba.data(),int(w),int(h),int(w)*4,QImage::Format_RGBA8888);p->drawImage(boundingRect(),image);
    if(!showMarker_)return;
    const auto marker=model_->picker().position(CM::Surface(field_),CM::Channel(channel_));
    const double insetX=std::min(5.,width()/2),insetY=std::min(5.,height()/2);
    const QPointF point(std::clamp(marker.x*std::max(1.,width()-1),insetX,width()-insetX),std::clamp(marker.y*std::max(1.,height()-1),insetY,height()-insetY));
    p->setBrush(Qt::NoBrush);p->setPen(QPen(QColor("#111111"),3));p->drawEllipse(point,5,5);p->setPen(QPen(Qt::white,1.5));p->drawEllipse(point,5,5);
}
void ColorManagerSurface::select(const QPointF& point) { if(model_)model_->editSurface(field_,point.x()/std::max(1.,width()-1),point.y()/std::max(1.,height()-1),channel_); }
void ColorManagerSurface::mousePressEvent(QMouseEvent* event) {
    if(!interactive_||!model_){event->ignore();return;}
    if(field_==ColorWheel&&std::hypot(event->position().x()/std::max(1.,width()-1)*2-1,1-event->position().y()/std::max(1.,height()-1)*2)>1){event->ignore();return;}
    pressed_=true;setKeepMouseGrab(true);forceActiveFocus(Qt::MouseFocusReason);select(event->position());event->accept();
}
void ColorManagerSurface::mouseMoveEvent(QMouseEvent* event) { if(pressed_){select(event->position());event->accept();}else event->ignore(); }
void ColorManagerSurface::mouseReleaseEvent(QMouseEvent* event) { if(pressed_){select(event->position());pressed_=false;setKeepMouseGrab(false);event->accept();}else event->ignore(); }
void ColorManagerSurface::keyPressEvent(QKeyEvent* event) {
    if(!interactive_||!model_){event->ignore();return;}
    auto point=model_->picker().position(CM::Surface(field_),CM::Channel(channel_));const double step=event->modifiers().testFlag(Qt::ShiftModifier)?.1:.01;
    if(event->key()==Qt::Key_Left)point.x-=step;else if(event->key()==Qt::Key_Right)point.x+=step;
    else if(event->key()==Qt::Key_Up)point.y-=step;else if(event->key()==Qt::Key_Down)point.y+=step;
    else {QQuickPaintedItem::keyPressEvent(event);return;}
    model_->editSurface(field_,point.x,point.y,channel_);event->accept();
}
void registerColorManagerTypes() {
    static const bool registered=[] {
        qmlRegisterType<ColorManagerModel>("Dreamscapes.Storage",1,0,"ColorManagerModel");
        qmlRegisterType<ColorManagerSurface>("Dreamscapes.Storage",1,0,"ColorManagerSurface");return true;
    }();Q_UNUSED(registered)
}
