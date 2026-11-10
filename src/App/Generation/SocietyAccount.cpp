#include "SocietyAccount.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <exception>
#include <utility>

using namespace iisacc::accounts::native;

SocietyAccount::SocietyAccount(bool enforce, const QString &socketPath, QObject *parent)
    : QObject(parent), enforced_(enforce),
      device_(detected_device("com.iisacc.dreamscapes",
          QCoreApplication::applicationVersion().isEmpty() ? "unknown" : QCoreApplication::applicationVersion().toStdString(), "")) {
    if (!enforced_) return;
    if (device_.id.size() != 64) { error_ = QStringLiteral("device_identity_unavailable"); return; }
    SocietySessionOptions options{.product_id="dreamscapes", .device=device_};
    if (!socketPath.isEmpty()) options.socket_path = socketPath.toStdString();
    initialize(std::move(options));
}
SocietyAccount::SocietyAccount(SocietySessionOptions options, QObject *parent)
    : QObject(parent), device_(options.device) { initialize(std::move(options)); }
void SocietyAccount::initialize(SocietySessionOptions options) {
    try { session_ = std::make_unique<SocietySession>(loop_, std::move(options)); }
    catch (const std::exception &) { error_ = QStringLiteral("society_unavailable"); return; }
    session_->changed = [this](auto snapshot) {
        if (launchGranted_) return;
        snapshot_ = std::move(snapshot);
        if (snapshot_.licensed) {
            launchGranted_ = true;
            QTimer::singleShot(0, this, [this] {
                if (launchGranted_) { pump_.stop(); session_.reset(); }
            });
        }
        emit changed();
    };
    pump_.setInterval(15);
    connect(&pump_, &QTimer::timeout, this, [this] { (void)loop_.poll(); });
    pump_.start();
    // Begin the asynchronous request after construction, when the application
    // event loop can present its workspace. An earlier explicit refresh is safe.
    QTimer::singleShot(0, this, [this] {
        if (session_ && !launchGranted_) session_->refresh();
    });
}
SocietyAccount::~SocietyAccount() { pump_.stop(); session_.reset(); }
bool SocietyAccount::signedIn() const { return launchGranted_ ? snapshot_.connected : session_ && session_->snapshot().connected; }
bool SocietyAccount::licensed() const { return !enforced_ || launchGranted_ || (session_ && session_->snapshot().licensed); }
QString SocietyAccount::subject() const { return signedIn() ? QString::fromStdString(launchGranted_ ? snapshot_.subject : session_->snapshot().subject) : QString{}; }
QString SocietyAccount::code() const { return !error_.isEmpty() ? error_ : launchGranted_ ? QString::fromStdString(snapshot_.code) : session_ ? QString::fromStdString(session_->snapshot().code) : QString{}; }
void SocietyAccount::refresh() { if (!launchGranted_ && session_) session_->refresh(true); }
bool SocietyAccount::openSociety() { return QDesktopServices::openUrl(QUrl(QStringLiteral("society://"))); }
void SocietyAccount::invalidate() { if (!launchGranted_ && session_) session_->cancel(); }
