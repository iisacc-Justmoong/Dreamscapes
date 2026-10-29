#include "GenerationReviewModel.h"
#include "NativeReviewSession.h"
#include <QDateTime>
#include <QUrl>
#include <QJsonDocument>
#include <algorithm>

namespace {
QString sourceOf(const QVariantMap& entry) {
    for (const auto* key : {"documentSource", "mediaSource", "image", "imageSource", "path"}) {
        const auto value = entry.value(key).toString();
        if (!value.isEmpty()) return value;
    }
    return {};
}
bool imageEntry(const QVariantMap& entry) {
    return dreamscapes::review::Session::imageEligible(entry.value("mediaType").toString().toStdString(),
        entry.value("isImage", true).toBool(), !entry.value("documentSource").toString().isEmpty());
}
QString available(const QVariantMap& entry, const QString& key) {
    const auto value = entry.value(key);
    return !value.isValid() || value.isNull() || value.toString().isEmpty()
        ? GenerationReviewModel::tr("Unavailable") : value.toString();
}
QString modelName(const QVariantMap& entry) {
    if (!entry.value("modelName").toString().isEmpty()) return entry.value("modelName").toString();
    const auto model = entry.value("model");
    const auto object = model.toMap();
    for (const auto* key : {"name", "path", "id"}) if (!object.value(key).toString().isEmpty()) return object.value(key).toString();
    return available(entry, "model");
}
QString resolution(const QVariantMap& entry) {
    const auto width = entry.value("width").toInt(), height = entry.value("height").toInt();
    return width > 0 && height > 0 ? QStringLiteral("%1 × %2").arg(width).arg(height) : GenerationReviewModel::tr("Unavailable");
}
QString elapsed(const QVariantMap& entry) {
    const auto start = QDateTime::fromString(entry.value("startedAt").toString(), Qt::ISODateWithMs);
    const auto end = QDateTime::fromString(entry.value("finishedAt").toString(), Qt::ISODateWithMs);
    return start.isValid() && end.isValid() && start <= end
        ? QStringLiteral("%1 s").arg(start.msecsTo(end) / 1000., 0, 'f', 2) : GenerationReviewModel::tr("Unavailable");
}
}

int GenerationReviewModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_visible.size(); }
QHash<int,QByteArray> GenerationReviewModel::roleNames() const {
    return {{KeyRole,"reviewKey"},{EntryRole,"entry"},{ReviewStateRole,"reviewState"},{SelectedRole,"selected"},
        {FocusedRole,"focused"},{OrdinalRole,"ordinal"},{ThumbnailRole,"thumbnailSource"},{MediaTypeRole,"mediaType"},{TitleRole,"resultTitle"}};
}
QVariant GenerationReviewModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) return {};
    const auto& key = m_visible[index.row()];
    const auto entry = m_entries.value(key);
    switch (role) {
    case KeyRole: return key;
    case EntryRole: return entry;
    case ReviewStateRole: return reviewState(key);
    case SelectedRole: return m_selection.selected(key.toStdString());
    case FocusedRole: return focusedKey() == key;
    case OrdinalRole: return m_ordinals.value(key);
    case ThumbnailRole: return entry.value("thumbnailSource", entry.value("imageSource", entry.value("previewSource")));
    case MediaTypeRole: return entry.value("mediaType", QStringLiteral("Image"));
    case Qt::DisplayRole:
    case TitleRole: return tr("%1 · %2").arg(entry.value("mediaType", QStringLiteral("Image")).toString()).arg(m_ordinals.value(key), 2, 10, QLatin1Char('0'));
    default: return {};
    }
}
QString GenerationReviewModel::keyForEntry(const QVariantMap& entry) const {
    const auto alias = entry.value("alias").toMap();
    const auto container = entry.value("containerId", alias.value("containerId")).toString();
    const auto key = entry.value("key", alias.value("key")).toString();
    if (!container.isEmpty() && !key.isEmpty()) return container + '|' + key;
    auto source = sourceOf(entry);
    if (source.isEmpty()) return {};
    QUrl url(source); url.setQuery(QString{}); url.setFragment(QString{});
    return entry.value("id").toString() + '|' + url.toString();
}
QVariantList GenerationReviewModel::entries() const {
    QVariantList result;
    result.reserve(totalCount());
    for (const auto& id : m_selection.order()) result.append(m_entries.value(QString::fromStdString(id)));
    return result;
}
void GenerationReviewModel::setEntries(const QVariantList& entries) {
    QHash<QString,QVariantMap> normalized;
    std::vector<std::string> order;
    for (const auto& value : entries) {
        const auto entry = value.toMap();
        const auto key = keyForEntry(entry);
        if (key.isEmpty() || normalized.contains(key)) continue;
        normalized.insert(key, entry); order.push_back(key.toStdString());
    }
    if (normalized == m_entries) return;
    QSet<QString> metadataChanged;
    for (auto it = normalized.cbegin(); it != normalized.cend(); ++it)
        if (m_entries.value(it.key()) != it.value()) metadataChanged.insert(it.key());
    m_entries = std::move(normalized);
    m_selection.synchronize(order);
    if (m_entries.isEmpty()) m_selectionMode = false;
    const auto previousOrdinals = m_ordinals;
    m_ordinals.clear();
    int ordinal = 0;
    for (const auto& id : m_selection.order()) {
        const auto key = QString::fromStdString(id);
        m_ordinals.insert(key, ++ordinal);
        if (previousOrdinals.value(key) != ordinal) metadataChanged.insert(key);
    }
    publishState(true, metadataChanged);
    emit entriesChanged();
}
QString GenerationReviewModel::stateName(State state) {
    if (state == State::Shortlisted) return QStringLiteral("shortlisted");
    if (state == State::Rejected) return QStringLiteral("rejected");
    return QStringLiteral("unreviewed");
}
GenerationReviewModel::State GenerationReviewModel::parseState(const QString& state) {
    if (state == "shortlisted") return State::Shortlisted;
    if (state == "rejected") return State::Rejected;
    return State::Unreviewed;
}
QStringList GenerationReviewModel::projection() const {
    QStringList visible;
    for (const auto& id : m_selection.order()) {
        const auto key = QString::fromStdString(id);
        if (!m_jobFilter.isEmpty() && m_entries.value(key).value("id").toString() != m_jobFilter) continue;
        const bool video = m_entries.value(key).value("mediaType").toString() == "Video";
        if (m_filter == "all" || (m_filter == "images" && !video) || (m_filter == "videos" && video)
            || m_filter == stateName(m_selection.state(id))) visible.append(key);
    }
    if (m_sort == "newest") std::reverse(visible.begin(), visible.end());
    else if (m_sort == "seed") std::stable_sort(visible.begin(), visible.end(), [this](const auto& a, const auto& b) {
        const auto left = m_entries.value(a).value("seed"), right = m_entries.value(b).value("seed");
        if (!left.isValid() || left.isNull()) return false;
        if (!right.isValid() || right.isNull()) return true;
        return left.toLongLong() < right.toLongLong();
    });
    return visible;
}
GenerationReviewModel::RowState GenerationReviewModel::rowState(const QString& key) const {
    const auto id = key.toStdString();
    return {m_selection.state(id), m_selection.selected(id), m_selection.focusedKey() == id};
}
bool GenerationReviewModel::reviewFiltered() const {
    return m_filter == "shortlisted" || m_filter == "rejected" || m_filter == "unreviewed";
}
void GenerationReviewModel::publishFocus(const QString& previous) {
    // Focus cannot affect filtering, sorting, review counts or selection.
    const auto current = focusedKey();
    const int before = m_visibleIndices.value(previous, -1), after = m_visibleIndices.value(current, -1);
    if (before >= 0) m_rowStates[before].focused = false;
    if (after >= 0) m_rowStates[after].focused = true;
    if (before >= 0) emit dataChanged(index(before), index(before), {FocusedRole});
    if (after >= 0) emit dataChanged(index(after), index(after), {FocusedRole});
    emit stateChanged();
}
void GenerationReviewModel::publishState(bool reproject, const QSet<QString>& metadataChanged) {
    const auto next = reproject ? projection() : m_visible;
    m_selectedCount = m_shortlistedCount = m_rejectedCount = 0;
    for (const auto& id : m_selection.order()) {
        m_selectedCount += m_selection.selected(id);
        m_shortlistedCount += m_selection.state(id) == State::Shortlisted;
        m_rejectedCount += m_selection.state(id) == State::Rejected;
    }
    m_hiddenSelectedCount = m_selectedCount;
    for (const auto& key : next) m_hiddenSelectedCount -= m_selection.selected(key.toStdString());
    const int previousSize = m_visible.size();
    bool reset = false;
    if (next != m_visible) {
        emit projectionAboutToChange();
        const bool append = next.size() > m_visible.size()
            && std::equal(m_visible.begin(), m_visible.end(), next.begin());
        reset = !append;
        if (append) beginInsertRows({}, m_visible.size(), next.size() - 1);
        else beginResetModel();
        m_visible = next;
        m_visibleIndices.clear();
        for (int i = 0; i < m_visible.size(); ++i) m_visibleIndices.insert(m_visible[i], i);
        if (reset) m_rowStates.clear();
        for (int i = m_rowStates.size(); i < m_visible.size(); ++i) m_rowStates.append(rowState(m_visible[i]));
        if (append) endInsertRows();
        else endResetModel();
        emit projectionChanged();
    }
    // New/reset rows already expose their complete state through the model
    // structure notification. Existing rows need only the roles that changed.
    struct Change { int first, last; QList<int> roles; };
    QList<Change> changes;
    if (!reset) for (int i = 0; i < previousSize; ++i) {
        const auto state = rowState(m_visible[i]);
        const auto previous = m_rowStates[i];
        QList<int> roles;
        if (metadataChanged.contains(m_visible[i])) {
            // Empty roles means all roles under the Qt model contract.
        } else {
            if (previous.selected != state.selected) roles.append(SelectedRole);
            if (previous.focused != state.focused) roles.append(FocusedRole);
            if (previous.review != state.review) roles.append(ReviewStateRole);
            if (roles.isEmpty()) continue;
        }
        m_rowStates[i] = state;
        if (!changes.isEmpty() && changes.last().last + 1 == i && changes.last().roles == roles)
            changes.last().last = i;
        else changes.append({i, i, roles});
    }
    for (const auto& change : changes) emit dataChanged(index(change.first), index(change.last), change.roles);
    emit stateChanged();
}
void GenerationReviewModel::setFilter(const QString& filter) {
    if (m_filter == filter || !QStringList{"all","unreviewed","shortlisted","rejected","images","videos"}.contains(filter)) return;
    m_filter = filter; publishState(); emit filterChanged();
}
void GenerationReviewModel::setSort(const QString& sort) {
    if (m_sort == sort || !QStringList{"arrival","newest","seed"}.contains(sort)) return;
    m_sort = sort; publishState(); emit sortChanged();
}
void GenerationReviewModel::setJobFilter(const QString& job) {
    if (m_jobFilter == job) return;
    m_jobFilter = job; publishState(); emit filterChanged();
}
void GenerationReviewModel::setSelectionMode(bool enabled) {
    if (m_selectionMode == enabled) return;
    m_selectionMode = enabled; emit stateChanged();
}
int GenerationReviewModel::shortlistedCount() const {
    return m_shortlistedCount;
}
int GenerationReviewModel::rejectedCount() const {
    return m_rejectedCount;
}
int GenerationReviewModel::hiddenSelectedCount() const {
    return m_hiddenSelectedCount;
}
QVariantList GenerationReviewModel::selectedEntries() const {
    QVariantList result;
    for (const auto& key : m_selection.selectedKeys()) result.append(m_entries.value(QString::fromStdString(key)));
    return result;
}
QVariantList GenerationReviewModel::selectedImages() const {
    QVariantList result;
    for (const auto& value : selectedEntries()) if (imageEntry(value.toMap())) result.append(value);
    return result;
}
std::vector<std::string> GenerationReviewModel::actionKeys() const {
    auto ids = m_selection.selectedKeys();
    if (ids.empty() && !m_selection.focusedKey().empty()) ids.push_back(m_selection.focusedKey());
    return ids;
}
QVariantList GenerationReviewModel::actionEntries() const {
    QVariantList result;
    for (const auto& key : actionKeys()) result.append(m_entries.value(QString::fromStdString(key)));
    return result;
}
QString GenerationReviewModel::focusedKey() const { return QString::fromStdString(m_selection.focusedKey()); }
int GenerationReviewModel::focusedIndex() const { return indexForKey(focusedKey()); }
QVariantMap GenerationReviewModel::focusedEntry() const { return m_entries.value(focusedKey()); }
QVariantMap GenerationReviewModel::entryAt(int row) const { return row >= 0 && row < m_visible.size() ? m_entries.value(m_visible[row]) : QVariantMap{}; }
int GenerationReviewModel::indexForKey(const QString& key) const { return m_visibleIndices.value(key, -1); }
bool GenerationReviewModel::isSelected(const QVariantMap& entry) const { return m_selection.selected(keyForEntry(entry).toStdString()); }
std::vector<std::string> GenerationReviewModel::visibleKeys() const {
    std::vector<std::string> keys; keys.reserve(m_visible.size());
    for (const auto& key : m_visible) keys.push_back(key.toStdString());
    return keys;
}
void GenerationReviewModel::selectAt(int row, int modifiers) {
    if (row < 0 || row >= m_visible.size()) return;
    using Mode = dreamscapes::review::SelectionMode;
    const bool add = modifiers & (Qt::ControlModifier | Qt::MetaModifier), range = modifiers & Qt::ShiftModifier;
    m_selection.select(m_visible[row].toStdString(), visibleKeys(), range ? (add ? Mode::AddRange : Mode::Range)
        : add ? Mode::Toggle : Mode::Replace);
    if (add || range) m_selectionMode = true;
    publishState(false);
}
void GenerationReviewModel::focusAt(int row) {
    const auto previous = focusedKey();
    if (row >= 0 && row < m_visible.size() && m_selection.focus(m_visible[row].toStdString())) publishFocus(previous);
}
void GenerationReviewModel::focusSource(const QString& source) {
    if (source.isEmpty()) return;
    auto requested=QUrl(source);requested.setQuery(QString{});requested.setFragment(QString{});
    for (const auto& id : m_selection.order()) {
        const auto entry = m_entries.value(QString::fromStdString(id));
        for (const auto* field : {"documentSource", "mediaSource", "image", "imageSource", "source", "path"}) {
            auto candidate=QUrl(entry.value(field).toString());candidate.setQuery(QString{});candidate.setFragment(QString{});
            if (!candidate.isEmpty() && candidate == requested) {
                const auto previous = focusedKey();
                if (m_selection.focus(id)) publishFocus(previous);
                return;
            }
        }
    }
}
void GenerationReviewModel::toggle(const QVariantMap& entry) {
    const auto key = keyForEntry(entry); auto keys = visibleKeys();
    if (std::find(keys.begin(), keys.end(), key.toStdString()) == keys.end()) keys.push_back(key.toStdString());
    m_selection.select(key.toStdString(), keys, dreamscapes::review::SelectionMode::Toggle);
    m_selectionMode = true; publishState(false);
}
void GenerationReviewModel::selectAll() { m_selection.selectAll(visibleKeys()); m_selectionMode = true; publishState(false); }
void GenerationReviewModel::clearSelection() { m_selection.clearSelection(); m_selectionMode = false; publishState(false); }
void GenerationReviewModel::toggleReview(const QString& state) { m_selection.toggleState(actionKeys(), parseState(state)); publishState(reviewFiltered()); }
void GenerationReviewModel::setReview(const QString& state) { m_selection.setState(actionKeys(), parseState(state)); publishState(reviewFiltered()); }
void GenerationReviewModel::setEntryReview(const QString& key, const QString& state) {
    m_selection.setState({key.toStdString()}, parseState(state)); publishState(reviewFiltered());
}
QString GenerationReviewModel::reviewState(const QString& key) const { return stateName(m_selection.state(key.toStdString())); }

QVariantList GenerationReviewModel::inspectorRows() const {
    const auto targets = actionEntries();
    if (targets.isEmpty()) return {};
    const auto combined = [&](const QString& label, auto valueFor) {
        QStringList values;
        for (const auto& target : targets) { const auto value = valueFor(target.toMap()); if (!values.contains(value)) values.append(value); }
        return QVariantMap{{"label",label},{"value",values.size()==1 ? values[0] : tr("Mixed · %1 values").arg(values.size())},
            {"mixed",values.size()>1},{"values",values}};
    };
    return {combined(tr("Model"), modelName), combined(tr("Seed"), [](const auto& e){return available(e,"seed");}),
        combined(tr("Resolution"), resolution), combined(tr("Generation time"), elapsed),
        combined(tr("Steps"), [](const auto& e){return available(e,"steps");}),
        combined(tr("Device"), [](const auto& e){return available(e,"device");}),
        combined(tr("Generation settings"), [](const QVariantMap& e){
            const auto parameters = e.value(e.value("mediaType") == "Video" ? "videoParameters" : "advancedParameters").toMap();
            return parameters.isEmpty() ? tr("Unavailable") : QString::fromUtf8(QJsonDocument::fromVariant(parameters).toJson(QJsonDocument::Indented));
        }),
        combined(tr("Prompt"), [](const auto& e){return available(e,"prompt");}),
        combined(tr("Negative prompt"), [](const auto& e){return available(e,"negativePrompt");})};
}

QVariantMap GenerationReviewModel::keyCommand(int key, int modifiers, int columns, bool overlay, bool detail, bool compare) {
    using namespace dreamscapes::vm;
    ReviewKeyInput input;
    switch (key) {
    case Qt::Key_Escape: input.key = ReviewKey::escape; break;
    case Qt::Key_A: input.key = ReviewKey::all; break;
    case Qt::Key_F: input.key = ReviewKey::shortlist; break;
    case Qt::Key_X: input.key = ReviewKey::reject; break;
    case Qt::Key_C: input.key = ReviewKey::compare; break;
    case Qt::Key_Space: input.key = ReviewKey::space; break;
    case Qt::Key_Return: case Qt::Key_Enter: input.key = ReviewKey::enter; break;
    case Qt::Key_Left: input.key = ReviewKey::left; break;
    case Qt::Key_Right: input.key = ReviewKey::right; break;
    case Qt::Key_Up: input.key = ReviewKey::up; break;
    case Qt::Key_Down: input.key = ReviewKey::down; break;
    case Qt::Key_Home: input.key = ReviewKey::home; break;
    case Qt::Key_End: input.key = ReviewKey::end; break;
    default: return {};
    }
    input.command = modifiers & (Qt::ControlModifier | Qt::MetaModifier);
    input.alt = modifiers & Qt::AltModifier; input.shift = modifiers & Qt::ShiftModifier;
    input.overlay = overlay; input.detail = detail; input.compare = compare;
    input.selected = selectedCount() > 0; input.selectionMode = selectionMode();
    input.count = rowCount(); input.focusedIndex = focusedIndex(); input.columns = columns;
    const auto plan = m_commands.keys().send(input); if (!plan) return {};
    QString action;
    switch (plan->action) {
    case ReviewAction::ignored: return {};
    case ReviewAction::back: action = "back"; break;
    case ReviewAction::clear: clearSelection(); break;
    case ReviewAction::selectAll: selectAll(); break;
    case ReviewAction::shortlist: toggleReview("shortlisted"); break;
    case ReviewAction::reject: toggleReview("rejected"); break;
    case ReviewAction::compare: action = "compare"; break;
    case ReviewAction::detail: if (focusedKey().isEmpty()) focusAt(0); action = "detail"; break;
    case ReviewAction::edit: action = "edit"; break;
    case ReviewAction::focus: if (input.shift) selectAt(plan->index, modifiers); else focusAt(plan->index); action = "focus"; break;
    }
    return {{"accepted", true}, {"action", action}, {"index", plan->index}};
}
bool GenerationReviewModel::validImages(const QVariantList &entries, int minimum, int maximum) const {
    return entries.size() >= minimum && entries.size() <= maximum && std::all_of(entries.begin(), entries.end(), [](const auto &value) { return imageEntry(value.toMap()); });
}
void GenerationReviewModel::markEntries(const QVariantList &entries, const QString &state) {
    std::vector<std::string> keys;
    for (const auto &entry : entries) keys.push_back(keyForEntry(entry.toMap()).toStdString());
    m_selection.setState(keys, parseState(state)); publishState(reviewFiltered());
}
QVariantList GenerationReviewModel::contextEntries() const { return isSelected(focusedEntry()) ? selectedEntries() : QVariantList{focusedEntry()}; }
QString GenerationReviewModel::filterLabel(const QString &key) const {
    if (key == "all") return tr("All %1").arg(totalCount());
    if (key == "shortlisted") return tr("Shortlisted %1").arg(shortlistedCount());
    if (key == "rejected") return tr("Rejected %1").arg(rejectedCount());
    if (key == "unreviewed") return tr("Unreviewed");
    if (key == "images") return tr("Images");
    if (key == "videos") return tr("Videos");
    return key;
}
