#include "NativeHomeRecentFilesPresenter.h"
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
#include <array>
#include <utility>

namespace dreamscapes::home {
namespace {
HomeRecentFilesStrings translatedStrings() {
    const auto text = [](const char *value) { return QCoreApplication::translate("HomeRecentFiles", value).toStdString(); };
    HomeRecentFilesStrings strings;
    strings.heading = text("Recent files"); strings.viewAll = text("View all");
    strings.empty = text("No recent files"); strings.loading = text("Reading Society…");
    strings.noSelection = text("No file selected"); strings.fileColumn = text("FILE");
    strings.specificationsColumn = text("SPECIFICATIONS"); strings.updatedColumn = text("UPDATED"); strings.stateColumn = text("STATE");
    strings.filters[0] = text("All types");
    strings.count = [](std::size_t count) { return QCoreApplication::translate("HomeRecentFiles", "%1 files").arg(count).toStdString(); };
    strings.selection = [](const std::string &name) {
        return QCoreApplication::translate("HomeRecentFiles", "1 selected · %1").arg(QString::fromStdString(name)).toStdString();
    };
    return strings;
}
QVariantList fileList(const QVariant &value) {
    if (value.metaType() == QMetaType::fromType<QJSValue>()) return value.value<QJSValue>().toVariant().toList();
    return value.toList();
}
}
NativeHomeRecentFilesPresenter::NativeHomeRecentFilesPresenter(QQuickWindow *window, Factory factory, QObject *parent)
    : QObject(parent), window_(window), factory_(std::move(factory)) {
    if (!window_ || !factory_ || window_->thread() != QThread::currentThread()) return;
    panel_ = window_->findChild<QQuickItem *>(QStringLiteral("desktopRecentFiles"));
    if (!panel_ || panel_->metaObject()->indexOfSignal("fileRequested(QVariant)") < 0 ||
        panel_->metaObject()->indexOfSignal("viewAllRequested()") < 0) return;
    const auto slot = metaObject()->method(metaObject()->indexOfSlot("synchronize()"));
    for (const auto *name : std::array{"files", "loading", "selectedKey", "typeFilter"}) {
        const auto index = panel_->metaObject()->indexOfProperty(name);
        if (index < 0) return;
        const auto property = panel_->metaObject()->property(index);
        if (!property.hasNotifySignal() || !QObject::connect(panel_, property.notifySignal(), this, slot)) return;
    }
    region_ = std::make_unique<native::NativePanelRegion>(window_, panel_, native::NativePanelRegion::Surface{
        .show = [this](QRectF frame) { return show(frame); }, .hide = [this] { hide(); }});
    overlays_ = std::make_unique<native::NativePanelOverlays>(window_, [this](bool blocked) { region_->setBlocked(blocked); });
    connect(window_, &QObject::destroyed, this, [this] { shutdown({}); });
    connect(panel_, &QObject::destroyed, this, [this] { shutdown({}); });
    if (qApp) qApp->installEventFilter(this);
    valid_ = true;
}
NativeHomeRecentFilesPresenter::~NativeHomeRecentFilesPresenter() {
    destroying_ = true;
    if (qApp) qApp->removeEventFilter(this);
    if (region_) region_->close();
    if (auto surface = surface_) surface->close();
    clearSession();
}
void NativeHomeRecentFilesPresenter::begin() {
    if (!valid_ || started_ || stopping_) return;
    started_ = true; region_->setReady(true);
}
bool NativeHomeRecentFilesPresenter::show(QRectF frame) {
    if (stopping_ || restarting_ || failed_ || !window_ || !panel_) return false;
    if (!surface_) {
        try {
            const QPointer<NativeHomeRecentFilesPresenter> self(this);
            session_ = std::make_unique<NativeHomeRecentFilesSession>(translatedStrings(),
                NativeHomeRecentFilesSession::Callbacks{
                    .select = [self](QString key) { if (self) self->select(std::move(key)); },
                    .open = [self](QVariant file) { if (self) self->open(std::move(file)); },
                    .browse = [self] { if (self) self->browse(); },
                    .filter = [self](QString key) { if (self) self->filter(std::move(key)); }});
            model_ = session_->model(); synchronize();
            if (!self || failed_) return false;
            surface_ = factory_(model_, session_.get(), {.ready = [self] { if (self) self->ready(); },
                .failed = [self] { if (self) self->failed(); }, .closed = [self] { if (self) self->closed(); }});
            auto surface = surface_;
            if (!surface || !surface->start(frame)) { failed(); return false; }
        } catch (...) { failed(); }
        return false;
    }
    auto surface = surface_;
    if (!surfaceReady_ || !surface || !surface->show(frame)) return false;
    active_ = true; return true;
}
void NativeHomeRecentFilesPresenter::synchronize() {
    if (!session_ || !panel_ || *synchronizing_) return;
    auto gate = synchronizing_;
    struct Reset final { std::shared_ptr<bool> gate; ~Reset() { *gate = false; } } reset{gate};
    *gate = true;
    const QPointer<NativeHomeRecentFilesPresenter> self(this);
    try {
        const auto result = session_->synchronize(fileList(panel_->property("files")), panel_->property("loading").toBool(),
            panel_->property("selectedKey").toString(), panel_->property("typeFilter").toString());
        if (self && !result) failed();
    } catch (...) { if (self) failed(); }
}
void NativeHomeRecentFilesPresenter::hide() {
    if (!active_) return;
    active_ = false; if (surface_) surface_->hide();
}
void NativeHomeRecentFilesPresenter::select(QString key) {
    if (panel_ && !stopping_) panel_->setProperty("selectedKey", std::move(key));
}
void NativeHomeRecentFilesPresenter::filter(QString key) {
    if (panel_ && !stopping_) panel_->setProperty("typeFilter", std::move(key));
}
void NativeHomeRecentFilesPresenter::open(QVariant file) {
    if (!active_ || stopping_ || !panel_) return;
    if (surface_) surface_->restoreHostFocus();
    (void)QMetaObject::invokeMethod(panel_, "fileRequested", Qt::DirectConnection, Q_ARG(QVariant, std::move(file)));
}
void NativeHomeRecentFilesPresenter::browse() {
    if (!active_ || stopping_ || !panel_) return;
    if (surface_) surface_->restoreHostFocus();
    (void)QMetaObject::invokeMethod(panel_, "viewAllRequested", Qt::DirectConnection);
}
void NativeHomeRecentFilesPresenter::ready() {
    if (destroying_ || stopping_ || restarting_ || failed_) return;
    surfaceReady_ = true; region_->setReady(false); region_->setReady(true);
}
void NativeHomeRecentFilesPresenter::failed() {
    if (destroying_ || failed_) return;
    failed_ = true; surfaceReady_ = false; region_->setReady(false);
    if (auto surface = surface_) surface->close(); else clearSession();
}
void NativeHomeRecentFilesPresenter::closed() {
    if (destroying_) return;
    surfaceReady_ = false; active_ = false; surface_.reset(); clearSession();
    if (stopping_) { finishShutdown(); return; }
    if (restarting_) { restarting_ = false; failed_ = false; region_->setReady(true); }
    else { failed_ = true; region_->setReady(false); }
}
void NativeHomeRecentFilesPresenter::restartForLanguage() {
    if (!started_ || stopping_ || restarting_) return;
    restarting_ = true; surfaceReady_ = false; region_->setReady(false);
    if (auto surface = surface_) surface->close();
    else { clearSession(); restarting_ = false; failed_ = false; region_->setReady(true); }
}
bool NativeHomeRecentFilesPresenter::eventFilter(QObject *watched, QEvent *event) {
    if (watched == qApp && event->type() == QEvent::LanguageChange) restartForLanguage();
    return QObject::eventFilter(watched, event);
}
void NativeHomeRecentFilesPresenter::shutdown(std::function<void()> done) {
    if (done) completion_.push_back(std::move(done));
    if (stopping_) { if (!surface_) finishShutdown(); return; }
    stopping_ = true;
    if (region_) region_->close(); overlays_.reset();
    if (auto surface = surface_) surface->close(); else finishShutdown();
}
void NativeHomeRecentFilesPresenter::clearSession() { session_.reset(); model_.reset(); }
void NativeHomeRecentFilesPresenter::finishShutdown() {
    clearSession(); auto callbacks = std::move(completion_);
    for (auto &done : callbacks) if (done) done();
}
} // namespace dreamscapes::home
