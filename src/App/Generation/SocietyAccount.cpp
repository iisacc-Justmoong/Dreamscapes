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
        if (decisionMade_) return;
        snapshot_ = std::move(snapshot);
        if (!snapshot_.checking) {
            // The first completed, verified decision is immutable for this run.
            launchGranted_ = snapshot_.licensed;
            decisionMade_ = true;
            // Destroy the session after its callback returns. This also cancels
            // SocietySession's periodic refresh on denial or unavailability.
            QTimer::singleShot(0, this, [this] {
                if (decisionMade_) { pump_.stop(); session_.reset(); }
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
        refresh();
    });
}
SocietyAccount::~SocietyAccount() { pump_.stop(); session_.reset(); }
bool SocietyAccount::signedIn() const { return snapshot_.connected; }
bool SocietyAccount::licensed() const { return !enforced_ || launchGranted_; }
QString SocietyAccount::subject() const { return signedIn() ? QString::fromStdString(snapshot_.subject) : QString{}; }
QString SocietyAccount::code() const { return !error_.isEmpty() ? error_ : QString::fromStdString(snapshot_.code); }
void SocietyAccount::refresh() {
    if (!session_ || requestStarted_ || decisionMade_) return;
    requestStarted_ = true;
    session_->refresh(true);
}
bool SocietyAccount::openSociety() { return QDesktopServices::openUrl(QUrl(QStringLiteral("society://"))); }
void SocietyAccount::invalidate() { if (!decisionMade_ && session_) session_->cancel(); }
