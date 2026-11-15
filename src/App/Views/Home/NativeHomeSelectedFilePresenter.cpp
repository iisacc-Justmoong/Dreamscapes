#include "NativeHomeSelectedFilePresenter.h"
#include "NativeHomeSelectedFileSession.h"
#include "App/Native/NativePanelOverlays.h"
#include "App/Native/NativePanelRegion.h"
#include <QCoreApplication>
#include <QEvent>
#include <QJSValue>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <utility>

namespace dreamscapes::home {
namespace {
HomeSelectedFileStrings translatedStrings() {
    const auto text = [](const char *value) { return QCoreApplication::translate("HomeSelectedFile", value).toStdString(); };
    HomeSelectedFileStrings strings;
    strings.heading = text("Selected file"); strings.open = text("Open in editor");
    strings.download = text("Download and open"); strings.empty = text("Select a recent file");
    strings.noFile = text("No file selected"); strings.unavailable = text("Unavailable");
    strings.canvas = text("Canvas"); strings.structure = text("Structure"); strings.color = text("Color");
    return strings;
}
QVariantMap fileMap(const QVariant &value) {
    if (value.metaType() == QMetaType::fromType<QJSValue>()) return value.value<QJSValue>().toVariant().toMap();
    return value.toMap();
}
}
NativeHomeSelectedFilePresenter::NativeHomeSelectedFilePresenter(QQuickWindow *window, Factory factory, QObject *parent)
    : QObject(parent), window_(window), factory_(std::move(factory)) {
    if (!window_ || !factory_ || window_->thread() != QThread::currentThread()) return;
    selected_ = window_->findChild<QQuickItem *>(QStringLiteral("homeSelectedFile"));
    if (!selected_ || selected_->metaObject()->indexOfSignal("fileRequested(QVariant)") < 0) return;
    const auto index = selected_->metaObject()->indexOfProperty("file");
    if (index < 0) return;
    const auto property = selected_->metaObject()->property(index);
    const auto slot = metaObject()->method(metaObject()->indexOfSlot("synchronizeFile()"));
    if (!property.hasNotifySignal() || !QObject::connect(selected_, property.notifySignal(), this, slot)) return;
    region_ = std::make_unique<native::NativePanelRegion>(window_, selected_, native::NativePanelRegion::Surface{
        .show = [this](QRectF frame) { return show(frame); }, .hide = [this] { hide(); }});
    overlays_ = std::make_unique<native::NativePanelOverlays>(window_, [this](bool blocked) { region_->setBlocked(blocked); });
    connect(window_, &QObject::destroyed, this, [this] { shutdown({}); });
    connect(selected_, &QObject::destroyed, this, [this] { shutdown({}); });
    if (qApp) qApp->installEventFilter(this);
    valid_ = true;
}
NativeHomeSelectedFilePresenter::~NativeHomeSelectedFilePresenter() {
    destroying_ = true;
    if (qApp) qApp->removeEventFilter(this);
    if (region_) region_->close();
    if (auto surface = surface_) surface->close();
    clearSession();
}
void NativeHomeSelectedFilePresenter::begin() {
    if (!valid_ || started_ || stopping_) return;
    started_ = true; region_->setReady(true);
}
bool NativeHomeSelectedFilePresenter::show(QRectF frame) {
    if (stopping_ || restarting_ || failed_ || !window_ || !selected_) return false;
    if (!surface_) {
        try {
            const QPointer<NativeHomeSelectedFilePresenter> self(this);
            session_ = std::make_unique<NativeHomeSelectedFileSession>(translatedStrings(),
                [self](QVariantMap file) { if (self) self->openFile(std::move(file)); });
            model_ = session_->model();
            synchronizeFile();
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
void NativeHomeSelectedFilePresenter::synchronizeFile() {
    if (!session_ || !selected_) return;
    try { if (!session_->setFile(fileMap(selected_->property("file")))) failed(); }
    catch (...) { failed(); }
}
void NativeHomeSelectedFilePresenter::hide() {
    if (!active_) return;
    active_ = false;
    if (surface_) surface_->hide();
}
void NativeHomeSelectedFilePresenter::openFile(QVariantMap file) {
    if (!active_ || stopping_ || !selected_) return;
    if (surface_) surface_->restoreHostFocus();
    (void)QMetaObject::invokeMethod(selected_, "fileRequested", Qt::DirectConnection, Q_ARG(QVariant, QVariant(std::move(file))));
}
void NativeHomeSelectedFilePresenter::ready() {
    if (destroying_ || stopping_ || restarting_ || failed_) return;
    surfaceReady_ = true; region_->setReady(false); region_->setReady(true);
}
void NativeHomeSelectedFilePresenter::failed() {
    if (destroying_ || failed_) return;
    failed_ = true; surfaceReady_ = false; region_->setReady(false);
    if (auto surface = surface_) surface->close(); else clearSession();
}
void NativeHomeSelectedFilePresenter::closed() {
    if (destroying_) return;
    surfaceReady_ = false; active_ = false; surface_.reset(); clearSession();
    if (stopping_) { finishShutdown(); return; }
    if (restarting_) { restarting_ = false; failed_ = false; region_->setReady(true); }
    else { failed_ = true; region_->setReady(false); }
}
void NativeHomeSelectedFilePresenter::restartForLanguage() {
    if (!started_ || stopping_ || restarting_) return;
    restarting_ = true; surfaceReady_ = false; region_->setReady(false);
    if (auto surface = surface_) surface->close();
    else { clearSession(); restarting_ = false; failed_ = false; region_->setReady(true); }
}
bool NativeHomeSelectedFilePresenter::eventFilter(QObject *watched, QEvent *event) {
    if (watched == qApp && event->type() == QEvent::LanguageChange) restartForLanguage();
    return QObject::eventFilter(watched, event);
}
void NativeHomeSelectedFilePresenter::shutdown(std::function<void()> done) {
    if (done) completion_.push_back(std::move(done));
    if (stopping_) { if (!surface_) finishShutdown(); return; }
    stopping_ = true;
    if (region_) region_->close(); overlays_.reset();
    if (auto surface = surface_) surface->close(); else finishShutdown();
}
void NativeHomeSelectedFilePresenter::clearSession() {
    session_.reset(); model_.reset();
}
void NativeHomeSelectedFilePresenter::finishShutdown() {
    clearSession();
    auto callbacks = std::move(completion_);
    for (auto &done : callbacks) if (done) done();
}
} // namespace dreamscapes::home
