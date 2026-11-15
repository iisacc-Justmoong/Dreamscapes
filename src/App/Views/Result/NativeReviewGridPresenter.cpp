#include "NativeReviewGridPresenter.h"
#include "GenerationReviewModel.h"
#include "App/Native/NativePanelOverlays.h"
#include "App/Native/NativePanelRegion.h"
#include "App/ViewModels/Native/ViewValue.h"
#include <QCoreApplication>
#include <QEvent>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <utility>

namespace dreamscapes::review {
namespace {
QString translated(const char *text) {return QCoreApplication::translate("GenerationResult",text);}
ReviewGridStrings gridStrings() {
    return {[](std::int64_t ordinal,std::string_view media,std::string_view id) {
        const auto name=QString::fromUtf8(id.data(),qsizetype(id.size()));
        return translated("%1 · %2 · %3").arg(QString::number(ordinal).rightJustified(2,'0'),
            QString::fromUtf8(media.data(),qsizetype(media.size())),name.isEmpty()?translated("Unavailable"):name.left(8)).toStdString();
    },translated("Loading…").toStdString(),translated("Preview unavailable").toStdString()};
}
ReviewContextStrings menuStrings() {
    const auto count=[](const char *format,std::size_t n) {return translated(format).arg(qulonglong(n)).toStdString();};
    return {[count](std::size_t n){return count("Shortlist · %1 results",n);},
        [count](std::size_t n){return count("Reject · %1 results",n);},
        [count](std::size_t n){return count("Reset review · %1 results",n);},
        [count](std::size_t n){return count("Compare · %1 images",n);},
        [count](std::size_t n){return count("Edit · %1 images",n);},
        translated("Save current to File").toStdString(),translated("Save current to Photos").toStdString()};
}
bool watchProperty(QObject *source,const char *property,QObject *target,const char *slot) {
    if (!source) return false;
    const auto *meta=source->metaObject(); const auto index=meta->indexOfProperty(property);
    if (index<0) return false;
    const auto item=meta->property(index); const auto receiver=target->metaObject()->indexOfSlot(slot);
    return item.hasNotifySignal() && receiver>=0 && bool(QObject::connect(source,item.notifySignal(),target,target->metaObject()->method(receiver)));
}
}
NativeReviewGridPresenter::NativeReviewGridPresenter(QQuickWindow *window,GenerationReviewModel *review,Factory factory,QObject *parent)
    : QObject(parent),window_(window),review_(review),factory_(std::move(factory)) {
    if (!window_ || !review_ || !factory_ || window_->thread()!=QThread::currentThread() || review_->thread()!=QThread::currentThread()) return;
    root_=window_->findChild<QQuickItem *>(QStringLiteral("generationResult"));
    gallery_=root_?root_->findChild<QQuickItem *>(QStringLiteral("resultGallery")):nullptr;
    if (!root_ || !gallery_ || gallery_->metaObject()->indexOfProperty("contentY")<0 || gallery_->metaObject()->indexOfProperty("originY")<0) return;
    for (const auto method : {"showEntry(QVariant)","editEntries(QVariant)","compareImages(QVariant)","saveImageToFile()","saveImageToPhotos()"})
        if (root_->metaObject()->indexOfMethod(method)<0) return;
    if (!watchProperty(root_,"queueOpen",this,"refreshBlocked()") || !watchProperty(root_,"inspectorOpen",this,"refreshBlocked()")) return;
    fileExporter_=root_->findChild<QObject *>(QStringLiteral("imageFileExporter"));
    photoExporter_=root_->findChild<QObject *>(QStringLiteral("photoLibraryExporter"));
    if (fileExporter_) (void)watchProperty(fileExporter_,"busy",this,"refreshSaveAvailability()");
    if (photoExporter_) (void)watchProperty(photoExporter_,"busy",this,"refreshSaveAvailability()");
    region_=std::make_unique<native::NativePanelRegion>(window_,gallery_,native::NativePanelRegion::Surface{
        .show=[this](QRectF frame){return show(frame);},.hide=[this]{hide();}});
    overlays_=std::make_unique<native::NativePanelOverlays>(window_,[this](bool blocked){overlayBlocked_=blocked; refreshBlocked();});
    connect(window_,&QObject::destroyed,this,[this]{shutdown({});});
    connect(review_,&QObject::destroyed,this,[this]{shutdown({});});
    if (qApp) qApp->installEventFilter(this);
    valid_=true;
}
NativeReviewGridPresenter::~NativeReviewGridPresenter() {
    destroying_=true; if (qApp) qApp->removeEventFilter(this);
    if (region_) region_->close(); if (auto surface=surface_) surface->close(); clearSession();
}
void NativeReviewGridPresenter::begin() {
    if (!valid_ || started_ || stopping_) return;
    started_=true; refreshBlocked(); region_->setReady(true);
}
void NativeReviewGridPresenter::refreshBlocked() {
    if (!region_) return;
    const bool blocked=overlayBlocked_ || (root_ && (root_->property("queueOpen").toBool() || root_->property("inspectorOpen").toBool()));
    region_->setBlocked(blocked);
}
void NativeReviewGridPresenter::refreshSaveAvailability() {
    if (!galleryModel_ || !galleryModel_->active()) return;
    const QPointer<NativeReviewGridPresenter> self(this);
    const bool file=fileExporter_ && !fileExporter_->property("busy").toBool();
    const bool photo=photoExporter_ && photoExporter_->property("supported").toBool() && !photoExporter_->property("busy").toBool();
    galleryModel_->setSaveFileHandler(file?ReviewGalleryModel::Action{[self](auto values){if (self) self->save("saveImageToFile",values);}}:ReviewGalleryModel::Action{});
    if (!self || !galleryModel_ || !galleryModel_->active()) return;
    galleryModel_->setSavePhotosHandler(photo?ReviewGalleryModel::Action{[self](auto values){if (self) self->save("saveImageToPhotos",values);}}:ReviewGalleryModel::Action{});
}
bool NativeReviewGridPresenter::show(QRectF frame) {
    if (stopping_ || restarting_ || failed_ || !window_ || !review_ || !root_ || !gallery_) return false;
    const QPointer<NativeReviewGridPresenter> self(this);
    try {
        if (!galleryModel_) {
            galleryModel_=ReviewGalleryModel::create(review_->nativeSession());
            galleryModel_->contextMenu()->setStrings(menuStrings());
            galleryModel_->setDetailHandler([self](auto values){if (self) self->deliver("showEntry",values,false);});
            galleryModel_->setEditHandler([self](auto values){if (self) self->deliver("editEntries",values,true);});
            galleryModel_->setCompareHandler([self](auto values){if (self) self->deliver("compareImages",values,true);});
            contextObservation_=galleryModel_->contextMenu()->context.observe([self](const auto &value,std::uint64_t){if (self) self->captureContext(value);});
            refreshSaveAvailability(); if (!self) return false;
            grid_=ReviewGridModel::create(galleryModel_,160,gridStrings());
            surface_=factory_(grid_,{.ready=[self]{if (self) self->ready();},.failed=[self]{if (self) self->failed();},
                                    .closed=[self]{if (self) self->closed();}});
            if (!self) return false;
            auto surface=surface_; const bool started=surface && surface->start(frame);
            if (!self) return false;
            if (!started) {failed(); return false;}
            return false;
        }
        auto surface=surface_; if (!surfaceReady_ || !surface || !surface->show(frame)) return false;
        if (!self) return false;
        if (!active_) {
            const auto offset=std::max(0.0,gallery_->property("contentY").toDouble()-gallery_->property("originY").toDouble());
            if (!std::isfinite(offset) || !surface->setScrollOffset(offset)) {surface->hide(); return false;}
            if (!self) return false;
            active_=true;
        }
        return true;
    } catch (...) {if (self) self->failed(); return false;}
}
void NativeReviewGridPresenter::hide() {
    if (!active_) return; active_=false;
    auto surface=surface_;
    if (surface && gallery_) if (auto offset=surface->scrollOffset(); offset && std::isfinite(*offset) && *offset>=0)
        gallery_->setProperty("contentY",gallery_->property("originY").toDouble()+*offset);
    if (surface) surface->hide();
}
void NativeReviewGridPresenter::captureContext(const ReviewContextState &value) {
    if (value.generation==contextGeneration_) return;
    contextGeneration_=value.generation;
    contextValues_=value.targets; contextOriginals_=review_?review_->originalEntries(value.targets):QVariantList{};
}
QVariantList NativeReviewGridPresenter::originals(const std::vector<vm::Value> &values) const {
    if (!review_) return {};
    if (values==contextValues_ && contextOriginals_.size()==qsizetype(values.size())) return contextOriginals_;
    return review_->originalEntries(values);
}
void NativeReviewGridPresenter::deliver(const char *method,const std::vector<vm::Value> &values,bool list) {
    if (stopping_ || !root_ || !review_ || values.empty()) return;
    const auto entries=originals(values); if (entries.size()!=qsizetype(values.size())) return;
    const auto target=root_; if (surface_) surface_->restoreHostFocus();
    const QVariant argument=list?QVariant(entries):entries.front();
    if (target) (void)QMetaObject::invokeMethod(target,method,Qt::DirectConnection,Q_ARG(QVariant,argument));
}
void NativeReviewGridPresenter::save(const char *method,const std::vector<vm::Value> &values) {
    if (stopping_ || !root_ || !review_ || values.size()!=1) return;
    const QPointer<NativeReviewGridPresenter> self(this);
    const auto key=QString::fromStdString(Session::keyForEntry(values.front()));
    if (key.isEmpty()) return;
    if (review_->focusedKey()!=key) {
        std::string source;
        for (const auto field : {"documentSource","mediaSource","imageSource"})
            if (!(source=vm::text(vm::get(values.front(),field))).empty()) break;
        if (source.empty()) return;
        review_->focusSource(QString::fromStdString(source));
    }
    if (!self || !self->review_ || self->review_->focusedKey()!=key || !self->root_) return;
    const auto target=self->root_; if (self->surface_) self->surface_->restoreHostFocus();
    if (target) (void)QMetaObject::invokeMethod(target,method,Qt::DirectConnection);
}
void NativeReviewGridPresenter::ready() {
    if (destroying_ || stopping_ || restarting_ || failed_) return;
    const QPointer<NativeReviewGridPresenter> self(this);
    surfaceReady_=true; region_->setReady(false); if (self) region_->setReady(true);
}
void NativeReviewGridPresenter::failed() {
    if (destroying_ || failed_) return;
    const QPointer<NativeReviewGridPresenter> self(this);
    failed_=true; surfaceReady_=false; if (region_) region_->setReady(false);
    if (!self) return;
    if (auto surface=surface_) surface->close(); else clearSession();
}
void NativeReviewGridPresenter::closed() {
    if (destroying_) return;
    const QPointer<NativeReviewGridPresenter> self(this);
    surfaceReady_=false; active_=false; surface_.reset(); clearSession();
    if (!self) return;
    if (stopping_) {finishShutdown(); return;}
    if (restarting_) {restarting_=false; failed_=false; if (region_) region_->setReady(true);}
    else {failed_=true; if (region_) region_->setReady(false);}
}
void NativeReviewGridPresenter::restartForLanguage() {
    if (!started_ || stopping_ || restarting_) return;
    const QPointer<NativeReviewGridPresenter> self(this);
    restarting_=true; surfaceReady_=false; if (region_) region_->setReady(false);
    if (!self) return;
    if (auto surface=surface_) surface->close();
    else {clearSession(); if (!self) return; restarting_=false; failed_=false; if (region_) region_->setReady(true);}
}
bool NativeReviewGridPresenter::eventFilter(QObject *watched,QEvent *event) {
    if (watched==qApp && event->type()==QEvent::LanguageChange) restartForLanguage();
    return QObject::eventFilter(watched,event);
}
void NativeReviewGridPresenter::shutdown(std::function<void()> done) {
    if (done) completion_.push_back(std::move(done));
    if (stopping_) {if (!surface_) finishShutdown(); return;}
    const QPointer<NativeReviewGridPresenter> self(this);
    stopping_=true; if (region_) region_->close();
    if (!self) return;
    overlays_.reset();
    if (auto surface=surface_) surface->close(); else finishShutdown();
}
void NativeReviewGridPresenter::clearSession() {
    contextObservation_.reset(); contextValues_.clear(); contextOriginals_.clear(); contextGeneration_=0;
    auto grid=std::move(grid_); auto gallery=std::move(galleryModel_);
    if (grid && grid->active()) grid->close();
    if (gallery && gallery->active()) gallery->close();
}
void NativeReviewGridPresenter::finishShutdown() {
    auto callbacks=std::move(completion_); clearSession();
    for (auto &done : callbacks) if (done) done();
}
} // namespace dreamscapes::review
