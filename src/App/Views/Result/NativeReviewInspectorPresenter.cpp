#include "NativeReviewInspectorPresenter.h"
#include "GenerationReviewModel.h"
#include "App/Native/NativePanelOverlays.h"
#include "App/Native/NativePanelRegion.h"
#include <QCoreApplication>
#include <QEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <utility>

namespace dreamscapes::review {
namespace {
constexpr auto noActive=static_cast<std::size_t>(-1);
InspectorViewStrings translatedStrings() {
    const auto view=[](const char *text) { return QCoreApplication::translate("ReviewInspector",text).toStdString(); };
    InspectorViewStrings strings;
    strings.heading=view("INSPECTOR"); strings.focused=view("Focused result"); strings.preview=view("Preview");
    strings.expand=view("Expand"); strings.save=view("Save current"); strings.unavailable=view("Unavailable");
    strings.noResult=QCoreApplication::translate("ReviewMediaView","No result selected").toStdString();
    strings.selected=[](std::size_t count) {
        return QCoreApplication::translate("ReviewInspector","%1 selected").arg(qulonglong(count)).toStdString();
    };
    strings.field=[](std::string_view name) {
        const std::string key(name);
        return QCoreApplication::translate("GenerationReviewModel",key.c_str()).toStdString();
    };
    strings.job=[](std::string_view id) {
        return QCoreApplication::translate("ReviewInspector","Job · %1")
            .arg(QString::fromUtf8(id.data(),qsizetype(id.size()))).toStdString();
    };
    strings.original=[](std::string_view value) {
        return QCoreApplication::translate("ReviewInspector","Original · %1")
            .arg(QString::fromUtf8(value.data(),qsizetype(value.size()))).toStdString();
    };
    return strings;
}
ReviewMediaStrings translatedMediaStrings() {
    const auto text=[](const char *value){return QCoreApplication::translate("ReviewMediaView",value).toStdString();};
    return {text("No result selected"),text("Play"),text("Pause"),text("Replay"),
        [](std::int64_t position,std::int64_t duration) {
            return QCoreApplication::translate("ReviewMediaView","%1 / %2 s")
                .arg(double(position)/1000.,0,'f',1).arg(double(duration)/1000.,0,'f',1).toStdString();
        },text("Loading preview…"),text("Preview unavailable")};
}
bool inspectorSignals(QQuickItem *item) {
    const auto *meta=item->metaObject();
    return meta->indexOfSignal("previewRequested()")>=0 && meta->indexOfSignal("saveRequested()")>=0;
}
}
NativeReviewInspectorPresenter::NativeReviewInspectorPresenter(QQuickWindow *window,GenerationReviewModel *review,
        Factory factory,PlaybackFactory playbackFactory,QObject *parent)
    : QObject(parent),window_(window),review_(review),factory_(std::move(factory)),playbackFactory_(std::move(playbackFactory)) {
    if (!window_ || !review_ || !factory_ || window_->thread()!=QThread::currentThread() ||
        review_->thread()!=QThread::currentThread()) return;
    for (auto *preview : window_->findChildren<QQuickItem *>(QStringLiteral("reviewInspectorPreview"))) {
        auto *panel=preview->parentItem();
        while (panel && !inspectorSignals(panel)) panel=panel->parentItem();
        if (!panel || std::any_of(panels_.begin(),panels_.end(),[panel](const auto &item){return item==panel;})) continue;
        panels_.push_back(panel); previews_.push_back(preview);
    }
    if (panels_.empty()) return;
    for (std::size_t i=0;i<panels_.size();++i)
        regions_.push_back(std::make_unique<native::NativePanelRegion>(window_,panels_[i],native::NativePanelRegion::Surface{
            .show=[this,i](QRectF frame){return show(i,frame);},.hide=[this,i]{hide(i);}}));
    overlays_=std::make_unique<native::NativePanelOverlays>(window_,[this](bool blocked) {
        for (const auto &region : regions_) region->setBlocked(blocked);
    });
    connect(window_,&QObject::destroyed,this,[this]{shutdown({});});
    connect(review_,&QObject::destroyed,this,[this]{shutdown({});});
    if (qApp) qApp->installEventFilter(this);
    valid_=true;
}
NativeReviewInspectorPresenter::~NativeReviewInspectorPresenter() {
    destroying_=true; if (qApp) qApp->removeEventFilter(this);
    for (const auto &region : regions_) region->close();
    if (auto surface=surface_) surface->close();
    clearSession();
}
void NativeReviewInspectorPresenter::begin() {
    if (!valid_ || started_ || stopping_) return;
    started_=true; updateAvailability();
}
bool NativeReviewInspectorPresenter::prepare(std::size_t index) {
    if (inspector_) return true;
    if (!review_ || index>=panels_.size() || !panels_[index] || !previews_[index]) return false;
    const auto height=std::clamp(double(panels_[index]->height()),1.0,4096.0);
    const auto preview=std::clamp(double(previews_[index]->height()),1.0,4096.0);
    inspector_=ReviewInspectorModel::create(review_->nativeSession());
    model_=InspectorViewModel::create(inspector_,translatedStrings(),{preview,1,height,height});
    media_=ReviewMediaModel::create(translatedMediaStrings());
    const QPointer<NativeReviewInspectorPresenter> self(this);
    media_->setFailureHandler([self](std::string message){if (self) self->mediaFailure(std::move(message));});
    inspector_->setExpandHandler([self](vm::Value){if (self) self->dispatch("previewRequested");});
    inspector_->setSaveHandler([self](vm::Value){if (self) self->dispatch("saveRequested");});
    videoObservation_=inspector_->video.observe([self](bool,std::uint64_t){if (self) self->scheduleMediaUpdate();});
    previewObservation_=inspector_->previewSource.observe([self](const auto &,std::uint64_t){if (self) self->scheduleMediaUpdate();});
    mediaObservation_=inspector_->mediaSource.observe([self](const auto &,std::uint64_t){if (self) self->scheduleMediaUpdate();});
    fieldsObservation_=model_->fields.observe([self](const auto &,std::uint64_t){if (self) self->observeFields();});
    refreshMedia(); if (self) observeFields();
    return bool(self) && bool(model_);
}
void NativeReviewInspectorPresenter::scheduleMediaUpdate() {
    if (!media_ || !inspector_ || stopping_ || restarting_ || mediaPending_) return;
    mediaPending_=true;
    (void)media_->setVisible(false);
    updateAvailability();
    const QPointer<NativeReviewInspectorPresenter> self(this);
    QTimer::singleShot(0,this,[self] {if (self) self->refreshMedia();});
}
void NativeReviewInspectorPresenter::refreshMedia() {
    if (!media_ || !inspector_ || stopping_ || restarting_) return;
    const QPointer<NativeReviewInspectorPresenter> self(this);
    const auto video=inspector_->video.get();
    const auto source=inspector_->mediaSource.get();
    if (video!=loadedVideo_ || source!=loadedMedia_) {mediaFailed_=false; loadedVideo_=video; loadedMedia_=source;}
    if (video && !playbackReady_ && playbackFactory_) {
        try {
            auto playback=playbackFactory_();
            if (playback && media_->setPlayback(std::move(playback))) playbackReady_=true;
        } catch (...) {playbackReady_=false;}
    }
    if (!self) return;
    const auto changed=media_->setResult(video,inspector_->previewSource.get(),source);
    if (!self) return;
    if (!changed) mediaFailed_=true;
    mediaPending_=false; updateAvailability();
}
void NativeReviewInspectorPresenter::mediaFailure(std::string message) {
    if (stopping_ || restarting_ || !media_) return;
    const QPointer<NativeReviewInspectorPresenter> self(this);
    mediaFailed_=true;
    if (active_<panels_.size() && panels_[active_]) {
        const auto panel=panels_[active_];
        if (surface_) surface_->restoreHostFocus();
        (void)QMetaObject::invokeMethod(panel,"playbackFailed",Qt::DirectConnection,
                                        Q_ARG(QString,QString::fromStdString(message)));
    }
    if (self) self->updateAvailability();
}
void NativeReviewInspectorPresenter::observeFields() {
    fieldObservations_.clear();
    if (!model_ || !model_->active()) return;
    const QPointer<NativeReviewInspectorPresenter> self(this);
    for (const auto &field : model_->fields.get())
        fieldObservations_.push_back(field->usesWrappedText.observe([self](bool,std::uint64_t){if (self) self->updateAvailability();}));
    updateAvailability();
}
void NativeReviewInspectorPresenter::updateAvailability() {
    const bool supported=started_ && !stopping_ && !restarting_ && !failed_ && !mediaPending_ && !mediaFailed_ &&
        (!inspector_ || ((!inspector_->video.get() || playbackReady_) && model_ && model_->active() &&
            std::all_of(model_->fields.get().begin(),model_->fields.get().end(),[](const auto &field){return field->usesWrappedText.get();})));
    for (const auto &region : regions_) region->setReady(supported);
}
bool NativeReviewInspectorPresenter::show(std::size_t index,QRectF frame) {
    if (stopping_ || restarting_ || failed_ || !window_ || !review_ || index>=panels_.size()) return false;
    try {
        const QPointer<NativeReviewInspectorPresenter> self(this);
        if (!prepare(index) || !self || mediaPending_ || mediaFailed_ || (inspector_->video.get() && !playbackReady_) ||
            std::any_of(model_->fields.get().begin(),model_->fields.get().end(),[](const auto &field){return !field->usesWrappedText.get();})) return false;
        if (!surface_) {
            surface_=factory_(model_,media_,{.ready=[self]{if (self) self->ready();},
                .failed=[self]{if (self) self->failed();},.closed=[self]{if (self) self->closed();}});
            auto surface=surface_;
            if (!surface || !surface->start(frame)) { failed(); return false; }
            return false;
        }
        auto surface=surface_;
        if (!surfaceReady_ || !surface) return false;
        // The legacy visual remains behind the transition surface. Stop any
        // audio it was playing before the native controls take ownership.
        if (inspector_->video.get() && previews_[index]) {
            if (auto *legacy=previews_[index]->findChild<QObject *>(QStringLiteral("reviewInspectorPreviewPlayer")))
                (void)QMetaObject::invokeMethod(legacy,"stop",Qt::DirectConnection);
        }
        if (!self || !surface->show(frame)) return false;
        active_=index;
        auto visible=media_->setVisible(true);
        if (!self) return false;
        if (!visible || mediaFailed_) {
            if (!visible && !mediaFailed_) mediaFailure(visible.error().message);
            if (!self) return false;
            active_=noActive; surface->hide(); return false;
        }
        return true;
    } catch (...) { failed(); return false; }
}
void NativeReviewInspectorPresenter::hide(std::size_t index) {
    if (active_!=index) return;
    active_=noActive; if (media_) (void)media_->setVisible(false); if (surface_) surface_->hide();
}
void NativeReviewInspectorPresenter::dispatch(const char *method) {
    if (stopping_ || active_>=panels_.size() || !panels_[active_]) return;
    const auto panel=panels_[active_];
    if (surface_) surface_->restoreHostFocus();
    (void)QMetaObject::invokeMethod(panel,method,Qt::DirectConnection);
}
void NativeReviewInspectorPresenter::ready() {
    if (destroying_ || stopping_ || restarting_ || failed_) return;
    surfaceReady_=true;
    for (const auto &region : regions_) region->setReady(false);
    updateAvailability();
}
void NativeReviewInspectorPresenter::failed() {
    if (destroying_ || failed_) return;
    failed_=true; surfaceReady_=false; updateAvailability();
    if (auto surface=surface_) surface->close(); else clearSession();
}
void NativeReviewInspectorPresenter::closed() {
    if (destroying_) return;
    surfaceReady_=false; active_=noActive; surface_.reset(); clearSession();
    if (stopping_) { finishShutdown(); return; }
    if (restarting_) { restarting_=false; failed_=false; updateAvailability(); }
    else { failed_=true; updateAvailability(); }
}
void NativeReviewInspectorPresenter::restartForLanguage() {
    if (!started_ || stopping_ || restarting_) return;
    restarting_=true; surfaceReady_=false; updateAvailability();
    if (auto surface=surface_) surface->close();
    else { clearSession(); restarting_=false; failed_=false; updateAvailability(); }
}
bool NativeReviewInspectorPresenter::eventFilter(QObject *watched,QEvent *event) {
    if (watched==qApp && event->type()==QEvent::LanguageChange) restartForLanguage();
    return QObject::eventFilter(watched,event);
}
void NativeReviewInspectorPresenter::shutdown(std::function<void()> done) {
    if (done) completion_.push_back(std::move(done));
    if (stopping_) { if (!surface_) finishShutdown(); return; }
    stopping_=true;
    for (const auto &region : regions_) region->close();
    overlays_.reset();
    if (auto surface=surface_) surface->close(); else finishShutdown();
}
void NativeReviewInspectorPresenter::clearSession() {
    videoObservation_.reset(); previewObservation_.reset(); mediaObservation_.reset(); fieldsObservation_.reset(); fieldObservations_.clear();
    if (media_) { media_->close(); media_.reset(); }
    loadedMedia_.clear(); loadedVideo_=false; playbackReady_=false; mediaPending_=false; mediaFailed_=false;
    if (model_) { model_->close(); model_.reset(); }
    if (inspector_) { inspector_->close(); inspector_.reset(); }
}
void NativeReviewInspectorPresenter::finishShutdown() {
    clearSession(); auto callbacks=std::move(completion_);
    for (auto &done : callbacks) if (done) done();
}
} // namespace dreamscapes::review
