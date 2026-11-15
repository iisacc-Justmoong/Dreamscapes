#include "NativeHomeRecentGenerationsPresenter.h"
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
#include <set>
#include <utility>

namespace dreamscapes::home {
namespace {
HomeRecentGenerationsStrings translatedStrings() {
    const auto text = [](const char *value) { return QCoreApplication::translate("HomeRecentGenerations", value).toStdString(); };
    HomeRecentGenerationsStrings strings;
    strings.heading = text("Recent generations"); strings.viewAll = text("View all");
    strings.empty = text("No generated images yet"); strings.loading = text("Reading Society…");
    strings.select = QCoreApplication::translate("ImageSelectionBar", "Select").toStdString();
    strings.cancel = QCoreApplication::translate("ImageSelectionBar", "Cancel").toStdString();
    strings.edit = [](std::size_t n) { return QCoreApplication::translate("ImageSelectionBar", "Edit (%1)").arg(n).toStdString(); };
    strings.count = [](std::size_t n) { return QCoreApplication::translate("HomeRecentGenerations", "%1 recent results").arg(n).toStdString(); };
    return strings;
}
QVariant normalized(QVariant value) {
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}
QStringList keys(QVariant value) {
    value = normalized(std::move(value));
    if (value.metaType() == QMetaType::fromType<QStringList>()) return value.toStringList();
    QStringList result;
    for (auto entry : value.toList()) result.push_back(normalized(std::move(entry)).toString());
    return result;
}
bool connectProperties(QObject *source, const std::vector<const char *> &names, QObject *receiver, QMetaMethod slot) {
    std::set<int> connected;
    for (const auto *name : names) {
        const auto index = source->metaObject()->indexOfProperty(name);
        if (index < 0) return false;
        const auto property = source->metaObject()->property(index);
        if (!property.hasNotifySignal()) return false;
        const auto signal = property.notifySignal();
        if (connected.insert(signal.methodIndex()).second && !QObject::connect(source, signal, receiver, slot)) return false;
    }
    return true;
}
}
NativeHomeRecentGenerationsPresenter::NativeHomeRecentGenerationsPresenter(QQuickWindow *window, Factory factory, QObject *parent)
    : QObject(parent), window_(window), factory_(std::move(factory)) {
    if (!window_ || !factory_ || window_->thread() != QThread::currentThread()) return;
    // The Home and full-history views share an object name. Choose the panel
    // by its commands and selection child rather than traversal order.
    for (auto *candidate : window_->findChildren<QQuickItem *>(QStringLiteral("generationHistory"))) {
        if (candidate->metaObject()->indexOfSignal("fileRequested(QVariant)") < 0 ||
            candidate->metaObject()->indexOfSignal("imagesRequested(QVariant)") < 0 ||
            candidate->metaObject()->indexOfSignal("viewAllRequested()") < 0) continue;
        auto *selection = candidate->findChild<QObject *>(QStringLiteral("generationHistorySelectionNativeModel"));
        if (selection) { panel_ = candidate; selection_ = selection; break; }
    }
    if (!selection_) return;
    const auto slot = metaObject()->method(metaObject()->indexOfSlot("synchronize()"));
    if (!connectProperties(panel_, {"files", "loading", "focusedKey"}, this, slot) ||
        !connectProperties(selection_, {"selectionMode", "selectedKeys"}, this, slot)) return;
    region_ = std::make_unique<native::NativePanelRegion>(window_, panel_, native::NativePanelRegion::Surface{
        .show = [this](QRectF frame) { return show(frame); }, .hide = [this] { hide(); }});
    overlays_ = std::make_unique<native::NativePanelOverlays>(window_, [this](bool blocked) { region_->setBlocked(blocked); });
    connect(window_, &QObject::destroyed, this, [this] { shutdown({}); });
    connect(panel_, &QObject::destroyed, this, [this] { shutdown({}); });
    connect(selection_, &QObject::destroyed, this, [this] { shutdown({}); });
    if (qApp) qApp->installEventFilter(this);
    valid_ = true;
}
NativeHomeRecentGenerationsPresenter::~NativeHomeRecentGenerationsPresenter() {
    destroying_ = true; if (qApp) qApp->removeEventFilter(this);
    if (region_) region_->close(); if (auto surface = surface_) surface->close(); clearSession();
}
void NativeHomeRecentGenerationsPresenter::begin() {
    if (!valid_ || started_ || stopping_) return;
    started_ = true; region_->setReady(true);
}
bool NativeHomeRecentGenerationsPresenter::show(QRectF frame) {
    if (stopping_ || restarting_ || failed_ || !window_ || !panel_ || !selection_) return false;
    if (!surface_) {
        try {
            const QPointer<NativeHomeRecentGenerationsPresenter> self(this);
            session_ = std::make_unique<NativeHomeRecentGenerationsSession>(translatedStrings(),
                NativeHomeRecentGenerationsSession::Callbacks{
                    .focus = [self](QString key) { if (self) self->focus(std::move(key)); },
                    .open = [self](QVariant file) { if (self) self->open(std::move(file)); },
                    .edit = [self](QVariantList files) { if (self) self->edit(std::move(files)); },
                    .browse = [self] { if (self) self->browse(); },
                    .selection = [self](bool mode, QStringList keys) { if (self) self->selection(mode, std::move(keys)); }});
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
void NativeHomeRecentGenerationsPresenter::synchronize() {
    if (!session_ || !panel_ || !selection_ || *synchronizing_) return;
    auto gate = synchronizing_;
    struct Reset final { std::shared_ptr<bool> value; ~Reset() { *value = false; } } reset{gate}; *gate = true;
    const QPointer<NativeHomeRecentGenerationsPresenter> self(this);
    try {
        auto result = session_->synchronize(normalized(panel_->property("files")).toList(), panel_->property("loading").toBool(),
            panel_->property("focusedKey").toString(), selection_->property("selectionMode").toBool(), keys(selection_->property("selectedKeys")));
        if (self && !result) failed();
    } catch (...) { if (self) failed(); }
}
void NativeHomeRecentGenerationsPresenter::hide() { if (active_) { active_ = false; if (surface_) surface_->hide(); } }
void NativeHomeRecentGenerationsPresenter::focus(QString key) { if (panel_ && !stopping_) panel_->setProperty("focusedKey", std::move(key)); }
void NativeHomeRecentGenerationsPresenter::selection(bool mode, QStringList selected) {
    if (!selection_ || stopping_) return;
    auto target = selection_;
    auto gate = synchronizing_; const bool previous = *gate; *gate = true;
    struct Reset final { std::shared_ptr<bool> value; bool previous; ~Reset() { *value = previous; } } reset{gate, previous};
    target->setProperty("selectedKeys", QVariant::fromValue(std::move(selected)));
    if (target) target->setProperty("selectionMode", mode);
}
void NativeHomeRecentGenerationsPresenter::open(QVariant file) {
    if (!active_ || stopping_ || !panel_) return;
    if (surface_) surface_->restoreHostFocus();
    (void)QMetaObject::invokeMethod(panel_, "fileRequested", Qt::DirectConnection, Q_ARG(QVariant, std::move(file)));
}
void NativeHomeRecentGenerationsPresenter::edit(QVariantList files) {
    if (!active_ || stopping_ || !panel_) return;
    if (surface_) surface_->restoreHostFocus();
    (void)QMetaObject::invokeMethod(panel_, "imagesRequested", Qt::DirectConnection, Q_ARG(QVariant, QVariant(std::move(files))));
}
void NativeHomeRecentGenerationsPresenter::browse() {
    if (!active_ || stopping_ || !panel_) return;
    if (surface_) surface_->restoreHostFocus();
    (void)QMetaObject::invokeMethod(panel_, "viewAllRequested", Qt::DirectConnection);
}
void NativeHomeRecentGenerationsPresenter::ready() {
    if (destroying_ || stopping_ || restarting_ || failed_) return;
    surfaceReady_ = true; region_->setReady(false); region_->setReady(true);
}
void NativeHomeRecentGenerationsPresenter::failed() {
    if (destroying_ || failed_) return;
    failed_ = true; surfaceReady_ = false; region_->setReady(false);
    if (auto surface = surface_) surface->close(); else clearSession();
}
void NativeHomeRecentGenerationsPresenter::closed() {
    if (destroying_) return;
    surfaceReady_ = false; active_ = false; surface_.reset(); clearSession();
    if (stopping_) { finishShutdown(); return; }
    if (restarting_) { restarting_ = false; failed_ = false; region_->setReady(true); }
    else { failed_ = true; region_->setReady(false); }
}
void NativeHomeRecentGenerationsPresenter::restartForLanguage() {
    if (!started_ || stopping_ || restarting_) return;
    restarting_ = true; surfaceReady_ = false; region_->setReady(false);
    if (auto surface = surface_) surface->close();
    else { clearSession(); restarting_ = false; failed_ = false; region_->setReady(true); }
}
bool NativeHomeRecentGenerationsPresenter::eventFilter(QObject *watched, QEvent *event) {
    if (watched == qApp && event->type() == QEvent::LanguageChange) restartForLanguage();
    return QObject::eventFilter(watched, event);
}
void NativeHomeRecentGenerationsPresenter::shutdown(std::function<void()> done) {
    if (done) completion_.push_back(std::move(done));
    if (stopping_) { if (!surface_) finishShutdown(); return; }
    stopping_ = true; if (region_) region_->close(); overlays_.reset();
    if (auto surface = surface_) surface->close(); else finishShutdown();
}
void NativeHomeRecentGenerationsPresenter::clearSession() { session_.reset(); model_.reset(); }
void NativeHomeRecentGenerationsPresenter::finishShutdown() {
    clearSession(); auto callbacks = std::move(completion_); for (auto &done : callbacks) if (done) done();
}
} // namespace dreamscapes::home
