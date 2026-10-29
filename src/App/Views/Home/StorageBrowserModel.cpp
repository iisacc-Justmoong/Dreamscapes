#include "StorageBrowserModel.h"
#include "../../ViewModels/ViewData.h"
using namespace dreamscapes;
using namespace dreamscapes::vm;
namespace {
QVariantList restoreAliasPayloads(const Value &projection, const QVariantMap &originals) {
    QVariantList rows;
    for (const auto &entry : array(projection)) {
        auto row = viewValue(entry).toMap();
        const auto original = originals.value(row.value("key").toString()).toMap();
        for (auto it = original.begin(); it != original.end(); ++it)
            if (row.contains(it.key()) && nativeValue(it.value()) == nativeValue(row.value(it.key())))
                row[it.key()] = it.value();
        rows.push_back(std::move(row));
    }
    return rows;
}
} // namespace
StorageBrowserModel::~StorageBrowserModel() {
    (void)projector_.queries().close();
}
void StorageBrowserModel::setContainer(iiSocietyContainer::Container *container) {
    if (container_ == container && (container || identity_.isEmpty()))
        return;
    if (container_)
        disconnect(container_, nullptr, this, nullptr);
    container_ = container;
    identity_.clear();
    mirrorRevision_.reset();
    request_.aliases = Value(Array{});
    originalAliases_.clear();
    files_.clear();
    entries_.clear();
    emit sectionFilesChanged();
    emit entriesChanged();
    resetNavigation();
    if (container_)
        connect(container_, &QObject::destroyed, this, [this] { setContainer(nullptr); });
    if (container_)
        connect(container_, &iiSocietyContainer::Container::stateChanged, this, &StorageBrowserModel::updateSnapshot);
    updateSnapshot();
    emit containerChanged();
}
void StorageBrowserModel::setActive(bool active) {
    if (active_ != active) {
        active_ = active;
        if (active_) {
            updateSnapshot();
            if (dirty_ && !inFlight_)
                dispatch();
        } else {
            ++epoch_;
            dirty_ = true;
        }
        emit activeChanged();
    }
}
void StorageBrowserModel::setSection(const QString &value) {
    if (section() == value)
        return;
    request_.section = value.toStdString();
    resetNavigation();
    emit sectionChanged();
}
void StorageBrowserModel::setDirectory(const QString &value) {
    if (directory() == value)
        return;
    request_.directory = value.toStdString();
    schedule();
    emit directoryChanged();
}
void StorageBrowserModel::setQuery(const QString &value) {
    if (query() == value)
        return;
    request_.query = value.toStdString();
    schedule();
    emit queryChanged();
}
void StorageBrowserModel::setSortOrder(const QString &value) {
    if (sortOrder() == value)
        return;
    request_.sort = value.toStdString();
    schedule();
    emit sortOrderChanged();
}
void StorageBrowserModel::setSelectedKey(const QString &value) {
    if (selectedKey() == value)
        return;
    request_.selectedKey = value.toStdString();
    emit selectedKeyChanged();
}
QVariantMap StorageBrowserModel::selectedEntry() const {
    for (const auto &row : entries_)
        if (row.toMap().value("key").toString() == selectedKey())
            return row.toMap();
    return {};
}
void StorageBrowserModel::updateSnapshot() {
    if (!active_ || !container_)
        return;
    const auto identity = container_->containerId() + '|' + container_->rootPath();
    if (identity_ != identity) {
        identity_ = identity;
        mirrorRevision_.reset();
        if (!files_.isEmpty()) {
            files_.clear();
            emit sectionFilesChanged();
        }
        if (!entries_.isEmpty()) {
            entries_.clear();
            emit entriesChanged();
        }
        resetNavigation();
    }
    if (mirrorRevision_ && *mirrorRevision_ == container_->revision())
        return;
    mirrorRevision_ = container_->revision();
    const auto aliases = container_->available() ? container_->aliases() : QVariantList{};
    request_.aliases = nativeValue(aliases);
    originalAliases_.clear();
    for (const auto &alias : aliases) {
        const auto row = alias.toMap();
        originalAliases_[row.value("key").toString()] = row;
    }
    schedule();
}
void StorageBrowserModel::resetNavigation() {
    request_.directory.clear();
    request_.query.clear();
    request_.selectedKey.clear();
    notice_.clear();
    emit directoryChanged();
    emit queryChanged();
    emit selectedKeyChanged();
    emit noticeChanged();
    emit navigationResetRequested();
    schedule();
}
void StorageBrowserModel::up() {
    if (request_.directory.empty())
        return;
    const auto at = request_.directory.rfind('/');
    request_.directory = at == std::string::npos ? "" : request_.directory.substr(0, at);
    request_.selectedKey.clear();
    emit directoryChanged();
    emit selectedKeyChanged();
    emit positionResetRequested();
    schedule();
}
void StorageBrowserModel::activate(const QVariantMap &file) {
    if (file.isEmpty())
        return;
    setNotice({});
    if (file.value("isDirectory").toBool()) {
        setDirectory(file.value("relativePath").toString());
        setSelectedKey({});
        emit positionResetRequested();
    } else if (!file.value("key").toString().isEmpty())
        emit fileRequested(file);
}
void StorageBrowserModel::schedule() {
    ++epoch_;
    dirty_ = true;
    if (active_ && !inFlight_)
        dispatch();
}
void StorageBrowserModel::dispatch() {
    if (!dirty_)
        return;
    dirty_ = false;
    inFlight_ = true;
    emit busyChanged();
    const auto epoch = epoch_;
    const auto admitted = projector_.queries().post(viewRuntime(), request_, [this, epoch](auto reply) {
        // The native port's close barrier covers this transport call. Qt then
        // owns queued delivery and drops it if this view is destroyed.
        QMetaObject::invokeMethod(
            this, [this, epoch, reply = std::move(reply)]() mutable { receive(epoch, std::move(reply)); },
            Qt::QueuedConnection);
    });
    if (!admitted) {
        inFlight_ = false;
        dirty_ = true;
        setNotice(tr("File view is busy. Refresh to try again."));
        emit busyChanged();
    }
}
void StorageBrowserModel::receive(quint64 epoch, ii::runtime::QueryReply<StoragePresentation> reply) {
    inFlight_ = false;
    if (epoch == epoch_ && reply) {
        const auto files = restoreAliasPayloads(reply->files, originalAliases_);
        const auto entries = restoreAliasPayloads(reply->entries, originalAliases_);
        if (files_ != files) {
            files_ = files;
            emit sectionFilesChanged();
        }
        if (entries_ != entries) {
            entries_ = entries;
            emit entriesChanged();
            emit selectedKeyChanged();
        }
        if (!request_.selectedKey.empty() && selectedEntry().isEmpty())
            setSelectedKey({});
    }
    emit busyChanged();
    if (active_ && dirty_)
        dispatch();
}
QString StorageBrowserModel::downloadStatus(const QVariantMap &state) const {
    const auto phase = state.value("state").toString();
    if (phase == "preparing")
        return tr("Preparing download…");
    if (phase == "connecting")
        return tr("Connecting to drive…");
    if (phase == "verifying")
        return tr("Verifying file…");
    if (phase == "cancelled")
        return tr("Download cancelled");
    if (phase == "failed") {
        const auto error = state.value("error").toString();
        if (error == "host_session_unavailable")
            return tr("Sign in to access this drive, then retry here.");
        if (error == "object_changed_select_again" || error == "drive_file_changed")
            return tr("The file changed. Refresh files and try again.");
        if (error == "remote_container_changed")
            return tr("The selected drive changed. Select the file again.");
        if (error.contains("timeout") || error == "host_disconnected" || error == "host_transport_unavailable")
            return tr("The drive is unreachable. Check the connection and retry.");
        return tr("Download failed. Retry or refresh files.");
    }
    const auto total = state.value("total").toDouble();
    return total > 0 ? tr("Downloading… %1%").arg(int(std::floor(100 * state.value("completed").toDouble() / total)))
                     : tr("Downloading…");
}
