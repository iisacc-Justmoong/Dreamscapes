#include "SocialPublishingController.h"
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace iiSocialPublish;
namespace {
QString q(std::string_view value) { return QString::fromUtf8(value.data(), qsizetype(value.size())); }
std::string s(const QVariant& value) { return value.toString().toStdString(); }
std::filesystem::path localPath(const QVariant& value) {
    const auto text = value.toString();
    const QUrl url(text);
    const auto path = url.isLocalFile() ? url.toLocalFile() : text;
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}
bool parse(const QVariantMap& input, Post& post) {
    static const std::set<QString> keys{"text", "title", "link", "tags", "media", "coverImageUrl", "visibility", "contentWarning", "sensitive", "madeWithAi", "idempotencyKey"};
    for (auto it = input.cbegin(); it != input.cend(); ++it) if (!keys.contains(it.key())) return false;
    for (const auto& key : {"text", "title", "link", "coverImageUrl", "visibility", "contentWarning", "idempotencyKey"})
        if (input.contains(key) && input.value(key).metaType() != QMetaType::fromType<QString>()) return false;
    for (const auto& key : {"sensitive", "madeWithAi"})
        if (input.contains(key) && input.value(key).metaType() != QMetaType::fromType<bool>()) return false;
    if (input.contains("tags") && !input.value("tags").canConvert<QVariantList>()) return false;
    post.text = s(input.value("text")); post.title = s(input.value("title")); post.link = s(input.value("link"));
    post.coverImageUrl = s(input.value("coverImageUrl")); post.contentWarning = s(input.value("contentWarning"));
    post.idempotencyKey = s(input.value("idempotencyKey"));
    post.visibility = s(input.value("visibility", QStringLiteral("public")));
    post.sensitive = input.value("sensitive").toBool(); post.madeWithAi = input.value("madeWithAi").toBool();
    for (const auto& value : input.value("tags").toList()) {
        if (value.metaType() != QMetaType::fromType<QString>()) return false;
        post.tags.push_back(s(value));
    }
    if (input.contains("media") && !input.value("media").canConvert<QVariantList>()) return false;
    for (auto& value : input.value("media").toList()) {
        if (!value.canConvert<QVariantMap>()) return false;
        auto entry = value.toMap();
        static const std::set<QString> mediaKeys{"kind", "localFile", "publicUrl", "mimeType", "altText"};
        for (auto it = entry.cbegin(); it != entry.cend(); ++it) if (!mediaKeys.contains(it.key())) return false;
        for (auto it = entry.cbegin(); it != entry.cend(); ++it)
            if (it.value().metaType() != QMetaType::fromType<QString>() && !(it.key() == "localFile" && it.value().metaType() == QMetaType::fromType<QUrl>())) return false;
        auto kind = entry.value("kind", "image").toString();
        if (kind != "image" && kind != "video") return false;
        Media media;
        media.kind = kind == "image" ? MediaKind::Image : MediaKind::Video;
        media.mimeType = s(entry.value("mimeType", kind == "image" ? "image/png" : "video/mp4"));
        if (entry.contains("localFile")) media.localFile = localPath(entry.value("localFile"));
        media.publicUrl = s(entry.value("publicUrl")); media.altText = s(entry.value("altText"));
        post.media.push_back(std::move(media));
    }
    return true;
}
QVariantMap outcome(const Outcome& result) {
    return {{"published", result.published()}, {"error", q(errorName(result.error))}, {"message", q(result.message)},
        {"postId", q(result.postId)}, {"url", q(result.url)}, {"httpStatus", result.httpStatus},
        {"retryAfterSeconds", result.retryAfterSeconds}, {"requiresReconciliation", result.requiresReconciliation()}};
}
QVariantMap postSnapshot(const Post& p) {
    QVariantList media; QStringList tags;
    for (const auto& m : p.media) media.append(QVariantMap{{"kind",int(m.kind)},{"localFile",QString::fromStdU16String(m.localFile.u16string())},
        {"publicUrl",q(m.publicUrl)},{"mimeType",q(m.mimeType)},{"altText",q(m.altText)}});
    for (const auto& tag : p.tags) tags.append(q(tag));
    return {{"text",q(p.text)},{"title",q(p.title)},{"link",q(p.link)},{"tags",tags},{"media",media},
        {"visibility",q(p.visibility)},{"contentWarning",q(p.contentWarning)},{"sensitive",p.sensitive},{"madeWithAi",p.madeWithAi},
        {"coverImageUrl",q(p.coverImageUrl)},{"replyToId",q(p.replyToId)},{"language",q(p.language)},{"replyControl",q(p.replyControl)},
        {"categoryId",q(p.youtube.categoryId)},{"notifySubscribers",p.youtube.notifySubscribers},
        {"madeForKids",p.youtube.madeForKids ? QVariant(*p.youtube.madeForKids) : QVariant()},
        {"syntheticMedia",p.youtube.containsSyntheticMedia ? QVariant(*p.youtube.containsSyntheticMedia) : QVariant()}};
}
} // namespace

struct SocialPublishingController::State {
    struct Grant { Platform platform; Account account; };
    struct Job {
        QString id, accountId, dedupKey;
        QByteArray fingerprint;
        Platform platform;
        Post post;
        std::optional<PublicationPlan> plan;
        Account account;
        std::stop_source stop;
    };
    std::shared_ptr<Transport> transport;
    std::map<QString, Grant> accounts; // Accessed only on the GUI thread.
    std::map<QString, std::uint64_t> accountRevisions;
    std::uint64_t accountSequence = 0;
    QVariantList rows;
    QString error;
    // All queue fields below are protected. No QObject/QML work occurs under it.
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<Job>> queue;
    std::shared_ptr<Job> active;
    bool workerStarted = false; // GUI thread only.
    bool closing = false; // Protected by mutex.
    std::map<QString, std::pair<QByteArray, QString>> dedup;
};

SocialPublishingController::SocialPublishingController(QObject* parent)
    : SocialPublishingController(std::make_shared<CurlTransport>(), parent) {}
SocialPublishingController::SocialPublishingController(std::shared_ptr<Transport> transport, QObject* parent)
    : QObject(parent), d(std::make_shared<State>()) { d->transport = std::move(transport); }
SocialPublishingController::~SocialPublishingController() {
    const auto state = d;
    std::vector<std::shared_ptr<State::Job>> retiring;
    {
        std::lock_guard lock(state->mutex);
        state->closing = true;
        retiring.assign(state->queue.begin(), state->queue.end());
        if (state->active) retiring.push_back(state->active);
        state->queue.clear();
    }
    // A stop callback may reenter a queue; do not invoke one under its mutex.
    for (const auto& job : retiring) job->stop.request_stop();
    state->wake.notify_all();
    // The value-owned worker retains its transport/grant until it returns.
    // Destroying a GUI owner never joins a provider's blocking network call.
}
QVariantList SocialPublishingController::providers() const {
    QVariantList out;
    for (auto& cap : capabilities()) {
        QStringList scopes;
        for (auto& scope : cap.requiredScopes) scopes.append(q(scope));
        out.append(QVariantMap{{"id", q(cap.id)}, {"name", q(cap.name)}, {"supported", cap.officialPublishingApi},
            {"text", cap.text}, {"images", cap.images}, {"video", cap.video}, {"requiresPublicMediaUrl", cap.requiresPublicMediaUrl},
            {"requiredScopes", scopes}, {"documentation", q(cap.documentation)}, {"requirement", q(cap.requirement)}});
    }
    return out;
}
QVariantList SocialPublishingController::jobs() const { return d->rows; }
QVariantList SocialPublishingController::accounts() const {
    QVariantList rows;
    for (const auto& [id,grant] : d->accounts) for (const auto& cap : capabilities()) if (cap.platform==grant.platform)
        rows.append(QVariantMap{{"id",id},{"provider",q(cap.id)},{"destinationId",q(grant.account.targetId)}});
    return rows;
}
QString SocialPublishingController::errorString() const { return d->error; }
int SocialPublishingController::pendingCount() const {
    return int(std::ranges::count_if(d->rows, [](const QVariant& row) { auto state = row.toMap().value("state"); return state == "queued" || state == "running"; }));
}
void SocialPublishingController::configureAccount(const QString& id, Platform platform, Account account) {
    if (id.isEmpty()) return;
    const auto state = d; const QPointer<SocialPublishingController> owner(this);
    const auto revision = ++state->accountSequence; state->accountRevisions[id] = revision;
    retireAccount(id);
    const auto current = state->accountRevisions.find(id);
    if (!owner || current == state->accountRevisions.end() || current->second != revision) return;
    state->accounts.insert_or_assign(id, State::Grant{platform, std::move(account)});
    state->accountRevisions.erase(current);
    emit changed();
}
void SocialPublishingController::disconnectAccount(const QString& id) {
    if (id.isEmpty()) return;
    const QPointer<SocialPublishingController> owner(this);
    const auto state = d; const auto revision = ++state->accountSequence;
    state->accountRevisions[id] = revision;
    retireAccount(id);
    const auto current = state->accountRevisions.find(id);
    if (current != state->accountRevisions.end() && current->second == revision) state->accountRevisions.erase(current);
    if (owner) emit changed();
}
void SocialPublishingController::retireAccount(const QString& id) {
    const auto state = d; const QPointer<SocialPublishingController> owner(this);
    state->accounts.erase(id);
    std::vector<QString> cancelled;
    std::vector<std::shared_ptr<State::Job>> retiring;
    {
        std::lock_guard lock(state->mutex);
        std::erase_if(state->queue, [&](const auto& job) {
            if (job->accountId != id) return false;
            retiring.push_back(job); cancelled.push_back(job->id); return true;
        });
        if (state->active && state->active->accountId == id) retiring.push_back(state->active);
    }
    for (const auto& job : retiring) job->stop.request_stop();
    if (!owner) return;
    // Queued jobs have made no request. Deliver their terminal outcome now,
    // outside the queue lock, independently of an unrelated active upload.
    for (const auto& job : cancelled) {
        Outcome result; result.error = Error::Cancelled;
        result.message = "Publishing account disconnected before execution";
        complete(job, std::move(result));
        if (!owner) return;
    }
}
QString SocialPublishingController::submit(const QString& accountId, const QVariantMap& input) {
    Post post;
    if (!parse(input,post)) { d->error=QStringLiteral("Unsupported post fields or media shape"); emit changed(); return {}; }
    return enqueue(accountId,std::move(post),{},QJsonDocument(QJsonObject::fromVariantMap(input)).toJson(QJsonDocument::Compact),{});
}
QString SocialPublishingController::submitPlan(const QString& accountId, PublicationPlan plan, const QString& destinationId) {
    auto grant=d->accounts.find(accountId);
    if (grant==d->accounts.end() || grant->second.platform!=plan.platform || !plan.ready()) {
        d->error=QStringLiteral("Choose a matching account and resolve review issues first"); emit changed(); return {};
    }
    QVariantList steps;
    for (const auto& step : plan.posts) steps.append(QVariantMap{{"post",postSnapshot(step.post)},{"replyTo",step.replyTo ? qlonglong(*step.replyTo) : -1}});
    const auto serialized=QJsonDocument::fromVariant(QVariantMap{{"steps",steps},{"destinationId",destinationId},{"scheduledAt",q(plan.scheduledAt)}}).toJson(QJsonDocument::Compact);
    auto post=plan.posts.front().post;
    return enqueue(accountId,std::move(post),std::move(plan),serialized,destinationId);
}
QString SocialPublishingController::enqueue(const QString& accountId, Post post, std::optional<PublicationPlan> plan, const QByteArray& serialized, const QString& destinationId) {
    d->error.clear();
    auto reject = [this](QString message) { d->error = std::move(message); emit changed(); return QString{}; };
    auto grant = d->accounts.find(accountId);
    if (grant == d->accounts.end()) return reject(QStringLiteral("A native OAuth publishing account must be configured first"));
    if (serialized.size() > 1024 * 1024) return reject(QStringLiteral("Post metadata exceeds 1 MiB"));
    auto fingerprint = QCryptographicHash::hash(serialized, QCryptographicHash::Sha256);
    const auto dedupKey = QString::fromLatin1(QJsonDocument(QJsonArray{accountId, q(post.idempotencyKey)}).toJson(QJsonDocument::Compact).toBase64());
    if (!post.idempotencyKey.empty()) {
        auto old = d->dedup.find(dedupKey);
        if (old != d->dedup.end()) {
            if (old->second.first != fingerprint) return reject(QStringLiteral("Idempotency key is already used by a different post"));
            const auto existingId = old->second.second;
            emit changed(); return existingId;
        }
    }
    if (pendingCount() >= 8) return reject(QStringLiteral("The publishing queue is full (8 jobs)"));
    if (d->rows.size() >= 256) return reject(QStringLiteral("Clear finished publishing jobs before submitting more"));
    auto job = std::make_shared<State::Job>();
    job->id = QUuid::createUuid().toString(QUuid::WithoutBraces); job->accountId = accountId;
    job->dedupKey = dedupKey; job->fingerprint = fingerprint;
    job->platform = grant->second.platform; job->account = grant->second.account;
    if (!destinationId.isEmpty()) job->account.targetId=destinationId.toStdString();
    if (!post.idempotencyKey.empty()) d->dedup.emplace(dedupKey, std::pair{fingerprint, job->id});
    else post.idempotencyKey = job->id.toStdString();
    job->post = std::move(post);
    job->plan = std::move(plan);
    std::string provider;
    for (auto& cap : capabilities()) if (cap.platform == job->platform) provider = cap.id;
    d->rows.append(QVariantMap{{"id", job->id}, {"accountId", accountId}, {"provider", q(provider)}, {"state", "queued"}});
    { std::lock_guard lock(d->mutex); d->queue.push_back(job); }
    if (!startWorker()) {
        { std::lock_guard lock(d->mutex); std::erase(d->queue, job); }
        Outcome result; result.error = Error::TransportFailure; result.message = "Publishing worker could not be started";
        complete(job->id, std::move(result)); return job->id;
    }
    d->wake.notify_one(); emit changed();
    return job->id;
}
bool SocialPublishingController::cancel(const QString& id) {
    std::shared_ptr<State::Job> retiring;
    bool active = false;
    {
        std::lock_guard lock(d->mutex);
        if (d->active && d->active->id == id) { retiring = d->active; active = true; }
        else {
            const auto queued = std::find_if(d->queue.begin(), d->queue.end(), [&](const auto& job) { return job->id == id; });
            if (queued == d->queue.end()) return false;
            retiring = *queued; d->queue.erase(queued);
        }
    }
    const QPointer<SocialPublishingController> owner(this);
    retiring->stop.request_stop();
    if (!owner || active) return true;
    Outcome result; result.error = Error::Cancelled;
    result.message = "Publishing cancelled before execution";
    complete(id, std::move(result));
    return true;
}
void SocialPublishingController::clearFinished() {
    std::set<QString> removed;
    d->rows.erase(std::remove_if(d->rows.begin(), d->rows.end(), [&](const QVariant& value) {
        auto row = value.toMap();
        if (row.value("state") == "queued" || row.value("state") == "running" || row.value("state") == "needs-reconciliation") return false;
        removed.insert(row.value("id").toString()); return true;
    }), d->rows.end());
    std::erase_if(d->dedup, [&](const auto& entry) { return removed.contains(entry.second.second); });
    emit changed();
}
bool SocialPublishingController::startWorker() {
    if (d->workerStarted) return true;
    const auto state = d; const QPointer<SocialPublishingController> owner(this);
    try {
        std::thread worker([state, owner] {
            std::map<Platform, std::unique_ptr<Publisher>> publishers;
            while (true) {
                std::shared_ptr<State::Job> job;
                {
                    std::unique_lock lock(state->mutex);
                    state->wake.wait(lock, [&] { return state->closing || !state->queue.empty(); });
                    if (state->closing) break;
                    job = state->queue.front(); state->queue.pop_front(); state->active = job;
                }
                QMetaObject::invokeMethod(QCoreApplication::instance(), [owner, id = job->id] {
                    if (!owner) return;
                    bool changed = false;
                    for (auto& value : owner->d->rows) {
                        auto row = value.toMap(); if (row.value("id") != id) continue;
                        if (row.value("state") == "queued") { row["state"] = "running"; value = row; changed = true; }
                        break;
                    }
                    if (changed) emit owner->changed();
                }, Qt::QueuedConnection);
                Outcome result;
                std::vector<Outcome> completed;
                std::size_t next=0;
                try {
                    auto& publisher = publishers[job->platform];
                    if (!publisher) publisher = makePublisher(job->platform);
                    Context context{*state->transport, job->stop.get_token()};
                    if (job->plan) {
                        auto sequence=publishPlan(*job->plan,job->account,context);
                        result=std::move(sequence.result); completed=std::move(sequence.completed); next=sequence.next;
                    } else result = publisher->publish(job->post, job->account, context);
                } catch (...) { result.error = Error::TransportFailure; result.message = "Publishing worker failed"; }
                { std::lock_guard lock(state->mutex); state->active.reset(); }
                QMetaObject::invokeMethod(QCoreApplication::instance(), [owner, id = job->id, result = std::move(result),completed=std::move(completed),next]() mutable {
                    if (owner) owner->complete(id, std::move(result),std::move(completed),next);
                }, Qt::QueuedConnection);
            }
        });
        worker.detach();
        state->workerStarted = true;
        return true;
    } catch (const std::system_error&) { return false; }
}
void SocialPublishingController::complete(const QString& id, Outcome result, std::vector<Outcome> completed, std::size_t next) {
    auto snapshot = outcome(result);
    const bool partial=!result.published() && !completed.empty();
    QVariantList steps; for (const auto& step : completed) steps.append(outcome(step));
    snapshot["completed"]=steps; snapshot["next"]=qulonglong(next);
    snapshot["requiresReconciliation"]=partial || result.requiresReconciliation();
    bool delivered = false;
    for (auto& value : d->rows) {
        auto row = value.toMap(); if (row.value("id") != id) continue;
        if (row.value("state") != "queued" && row.value("state") != "running") return;
        row["state"] = result.published() ? "published" : partial || result.requiresReconciliation() ? "needs-reconciliation" : result.error == Error::Cancelled ? "cancelled" : "failed";
        row["result"] = snapshot; value = row; delivered = true; break;
    }
    if (!delivered) return;
    const QPointer<SocialPublishingController> owner(this);
    emit jobFinished(id, snapshot);
    if (owner) emit changed();
}

QVariantList SocialPublishingController::accountsFor(const QString &platform) const {
    QVariantList result; for (const auto &account : accounts()) if (account.toMap().value("provider").toString() == platform) result.push_back(account); return result;
}
QStringList SocialPublishingController::providerNames() const {
    QStringList result; for (const auto &provider : providers()) result.push_back(provider.toMap().value("name").toString()); return result;
}
QString SocialPublishingController::providerId(const QString &name) const {
    for (const auto &provider : providers()) if (provider.toMap().value("name").toString() == name) return provider.toMap().value("id").toString(); return {};
}
QStringList SocialPublishingController::accountIds(const QString &platform) const {
    QStringList result; for (const auto &account : accountsFor(platform)) result.push_back(account.toMap().value("id").toString()); return result;
}
QString SocialPublishingController::matchingAccount(const QString &platform, const QString &preferred) const {
    const auto ids = accountIds(platform); return ids.contains(preferred) ? preferred : ids.isEmpty() ? QString() : ids.front();
}
