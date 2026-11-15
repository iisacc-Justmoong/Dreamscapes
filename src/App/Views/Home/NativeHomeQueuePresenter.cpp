#include "NativeHomeQueuePresenter.h"
#include "NativeHomeQueueSession.h"
#include "App/Native/NativePanelOverlays.h"
#include "App/Native/NativePanelRegion.h"
#include "App/ViewModels/WorkspaceSession.h"
#include <QCoreApplication>
#include <QEvent>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <utility>

namespace dreamscapes::home {
namespace {
HomeQueueStrings translatedStrings() {
    const auto text = [](const char *value) { return QCoreApplication::translate("HomeQueueBar", value).toStdString(); };
    HomeQueueStrings strings;
    strings.heading = text("Generation queue"); strings.openQueue = text("View queue");
    strings.currentSession = text("Current session"); strings.device = text("Device"); strings.model = text("Model");
    strings.execution = text("Execution"); strings.ready = text("Ready to generate"); strings.generating = text("Generating…");
    strings.queued = text("Queued"); strings.unavailable = text("Unavailable"); strings.running = text("Running");
    strings.idle = text("Idle"); strings.detail = text("Generation progress and failures appear here.");
    strings.counts = [](std::size_t running, std::size_t waiting, std::size_t failed) {
        return QCoreApplication::translate("HomeQueueBar", "%1 running · %2 waiting · %3 failed")
            .arg(qulonglong(running)).arg(qulonglong(waiting)).arg(qulonglong(failed)).toStdString();
    };
    return strings;
}
}
NativeHomeQueuePresenter::NativeHomeQueuePresenter(QQuickWindow *window, WorkspaceSession *workspace, Factory factory, QObject *parent)
    : QObject(parent), window_(window), workspace_(workspace), factory_(std::move(factory)) {
    if (!window_ || !workspace_ || !factory_ || window_->thread() != QThread::currentThread() ||
        workspace_->thread() != QThread::currentThread()) return;
    queue_ = window_->findChild<QQuickItem *>(QStringLiteral("homeQueueBar"));
    if (!queue_ || queue_->metaObject()->indexOfSignal("viewQueueRequested()") < 0) return;
    const auto slot = metaObject()->method(metaObject()->indexOfSlot("synchronizeNames()"));
    for (const auto *name : {"modelName", "deviceName"}) {
        const auto index = queue_->metaObject()->indexOfProperty(name);
        if (index < 0) return;
        const auto property = queue_->metaObject()->property(index);
        if (!property.hasNotifySignal() || !QObject::connect(queue_, property.notifySignal(), this, slot)) return;
    }
    region_ = std::make_unique<native::NativePanelRegion>(window_, queue_, native::NativePanelRegion::Surface{
        .show = [this](QRectF frame) { return show(frame); }, .hide = [this] { hide(); }});
    overlays_ = std::make_unique<native::NativePanelOverlays>(window_, [this](bool blocked) { region_->setBlocked(blocked); });
    connect(window_, &QObject::destroyed, this, [this] { shutdown({}); });
    connect(workspace_, &QObject::destroyed, this, [this] { shutdown({}); });
    connect(queue_, &QObject::destroyed, this, [this] { shutdown({}); });
    if (qApp) qApp->installEventFilter(this);
    valid_ = true;
}
NativeHomeQueuePresenter::~NativeHomeQueuePresenter() {
    destroying_ = true;
    if (qApp) qApp->removeEventFilter(this);
    if (region_) region_->close();
    if (auto surface = surface_) surface->close();
    clearSession();
}
void NativeHomeQueuePresenter::begin() {
    if (!valid_ || started_ || stopping_) return;
    started_ = true; region_->setReady(true);
}
bool NativeHomeQueuePresenter::show(QRectF frame) {
    if (stopping_ || restarting_ || failed_ || !window_ || !workspace_ || !queue_) return false;
    if (!surface_) {
        try {
            const QPointer<NativeHomeQueuePresenter> self(this);
            session_ = std::make_unique<NativeHomeQueueSession>(workspace_, translatedStrings(),
                [self] { if (self) self->openQueue(); });
            model_ = session_->model();
            synchronizeNames();
            if (failed_) return false;
            surface_ = factory_(model_, {.ready = [self] { if (self) self->ready(); },
                .failed = [self] { if (self) self->failed(); }, .closed = [self] { if (self) self->closed(); }});
            auto surface = surface_;
            if (!surface || !surface->start(frame)) { failed(); return false; }
        } catch (...) { failed(); }
        return false;
    }
    auto surface = surface_;
    if (!surfaceReady_ || !surface || !surface->show(frame)) return false;
    active_ = true;
    return true;
}
void NativeHomeQueuePresenter::synchronizeNames() {
    if (!session_ || !queue_) return;
    try { session_->setRuntimeNames(queue_->property("modelName").toString(), queue_->property("deviceName").toString()); }
    catch (...) { failed(); }
}
void NativeHomeQueuePresenter::hide() {
    if (!active_) return;
    active_ = false;
    if (surface_) surface_->hide();
}
void NativeHomeQueuePresenter::openQueue() {
    if (!active_ || stopping_ || !queue_) return;
    if (surface_) surface_->restoreHostFocus();
    (void)QMetaObject::invokeMethod(queue_, "viewQueueRequested", Qt::DirectConnection);
}
void NativeHomeQueuePresenter::ready() {
    if (destroying_ || stopping_ || restarting_ || failed_) return;
    surfaceReady_ = true; region_->setReady(false); region_->setReady(true);
}
void NativeHomeQueuePresenter::failed() {
    if (destroying_ || failed_) return;
    failed_ = true; surfaceReady_ = false; region_->setReady(false);
    if (auto surface = surface_) surface->close(); else clearSession();
}
void NativeHomeQueuePresenter::closed() {
    if (destroying_) return;
    surfaceReady_ = false; active_ = false; surface_.reset(); clearSession();
    if (stopping_) { finishShutdown(); return; }
    if (restarting_) { restarting_ = false; failed_ = false; region_->setReady(true); }
    else { failed_ = true; region_->setReady(false); }
}
void NativeHomeQueuePresenter::restartForLanguage() {
    if (!started_ || stopping_ || restarting_) return;
    restarting_ = true; surfaceReady_ = false; region_->setReady(false);
    if (auto surface = surface_) surface->close();
    else { clearSession(); restarting_ = false; failed_ = false; region_->setReady(true); }
}
bool NativeHomeQueuePresenter::eventFilter(QObject *watched, QEvent *event) {
    if (watched == qApp && event->type() == QEvent::LanguageChange) restartForLanguage();
    return QObject::eventFilter(watched, event);
}
void NativeHomeQueuePresenter::shutdown(std::function<void()> done) {
    if (done) completion_.push_back(std::move(done));
    if (stopping_) { if (!surface_) finishShutdown(); return; }
    stopping_ = true;
    if (region_) region_->close(); overlays_.reset();
    if (auto surface = surface_) surface->close(); else finishShutdown();
}
void NativeHomeQueuePresenter::clearSession() {
    session_.reset(); model_.reset();
}
void NativeHomeQueuePresenter::finishShutdown() {
    clearSession();
    auto callbacks = std::move(completion_);
    for (auto &done : callbacks) if (done) done();
}
} // namespace dreamscapes::home
