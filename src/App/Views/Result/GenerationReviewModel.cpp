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

#include <QMetaObject>
#include <cmath>
#include <stdexcept>


namespace {
using dreamscapes::vm::Value;
using dreamscapes::vm::Object;
using dreamscapes::vm::Array;

Value nativeReviewValue(const QVariant &value) {
    if (!value.isValid() || value.isNull()) return {};
    switch (value.metaType().id()) {
    case QMetaType::QVariant: return nativeReviewValue(value.value<QVariant>());
    case QMetaType::Bool: return Value(value.toBool());
    case QMetaType::Int: case QMetaType::LongLong: case QMetaType::Long: case QMetaType::Short:
        return Value(value.toLongLong());
    case QMetaType::UInt: case QMetaType::ULongLong: case QMetaType::ULong: case QMetaType::UShort:
        return Value(value.toULongLong());
    case QMetaType::Float: case QMetaType::Double:
        return std::isfinite(value.toDouble()) ? Value(value.toDouble()) : Value();
    case QMetaType::QDateTime: return Value(value.toDateTime().toUTC().toString(Qt::ISODateWithMs).toStdString());
    case QMetaType::QDate: return Value(value.toDate().toString(Qt::ISODate).toStdString());
    case QMetaType::QTime: return Value(value.toTime().toString(Qt::ISODateWithMs).toStdString());
    case QMetaType::QVariantMap: {
        Object result;
        const auto map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            result.emplace(it.key().toStdString(), nativeReviewValue(it.value()));
        return Value(std::move(result));
    }
    case QMetaType::QStringList: case QMetaType::QVariantList: {
        Array result;
        const auto list = value.toList();
        result.reserve(static_cast<std::size_t>(list.size()));
        for (const auto &item : list) result.push_back(nativeReviewValue(item));
        return Value(std::move(result));
    }
    default: return Value(value.toString().toStdString());
    }
}
QVariant qtReviewValue(const Value &value) {
    return std::visit([](const auto &v) -> QVariant {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::same_as<T, std::monostate>) return {};
        else if constexpr (std::same_as<T, std::string>) return QString::fromStdString(v);
        else if constexpr (std::same_as<T, Object>) {
            QVariantMap result;
            for (const auto &[key, item] : v) result.insert(QString::fromStdString(key), qtReviewValue(item));
            return result;
        } else if constexpr (std::same_as<T, Array>) {
            QVariantList result;
            result.reserve(static_cast<qsizetype>(v.size()));
            for (const auto &item : v) result.push_back(qtReviewValue(item));
            return result;
        } else if constexpr (std::same_as<T, ii::core::Bytes>)
            return QByteArray(reinterpret_cast<const char *>(v.data()), static_cast<qsizetype>(v.size()));
        else if constexpr (std::same_as<T, std::int64_t>) return QVariant::fromValue(static_cast<qlonglong>(v));
        else if constexpr (std::same_as<T, std::uint64_t>) return QVariant::fromValue(static_cast<qulonglong>(v));
        else return QVariant::fromValue(v);
    }, value.storage());
}
std::vector<Value> nativeReviewValues(const QVariantList &values) {
    std::vector<Value> result;
    result.reserve(static_cast<std::size_t>(values.size()));
    for (const auto &value : values) result.push_back(nativeReviewValue(value));
    return result;
}
}

GenerationReviewModel::GenerationReviewModel(QObject *parent)
    : QAbstractListModel(parent), m_session(std::make_shared<dreamscapes::review::Session>()) {
    m_observer = std::make_shared<dreamscapes::review::ChangePort>([this](const auto &change) { applyNativeChange(change); });
    if (const auto observed = m_session->observe(m_observer); !observed)
        throw std::runtime_error(observed.error().message);
}

bool GenerationReviewModel::deferDuringNotification(std::function<void()> action) {
    if (!m_notifying) return false;
    QMetaObject::invokeMethod(this, std::move(action), Qt::QueuedConnection);
    return true;
}
int GenerationReviewModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_visible.size(); }
QHash<int,QByteArray> GenerationReviewModel::roleNames() const {
    return {{KeyRole,"reviewKey"},{EntryRole,"entry"},{ReviewStateRole,"reviewState"},{SelectedRole,"selected"},
        {FocusedRole,"focused"},{OrdinalRole,"ordinal"},{ThumbnailRole,"thumbnailSource"},{MediaTypeRole,"mediaType"},{TitleRole,"resultTitle"}};
}
QVariant GenerationReviewModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) return {};
    const auto &key = m_visible[index.row()];
    const auto entry = m_entries.value(key);
    const auto &state = m_rowStates[index.row()];
    switch (role) {
    case KeyRole: return key;
    case EntryRole: return entry;
    case ReviewStateRole: return stateName(state.review);
    case SelectedRole: return state.selected;
    case FocusedRole: return state.focused;
    case OrdinalRole: return m_ordinals.value(key);
    case ThumbnailRole: return entry.value("thumbnailSource", entry.value("imageSource", entry.value("previewSource")));
    case MediaTypeRole: return entry.value("mediaType", QStringLiteral("Image"));
    case Qt::DisplayRole: case TitleRole:
        return tr("%1 · %2").arg(entry.value("mediaType", QStringLiteral("Image")).toString()).arg(m_ordinals.value(key), 2, 10, QLatin1Char('0'));
    default: return {};
    }
}
QString GenerationReviewModel::keyForEntry(const QVariantMap &entry) const {
    return QString::fromStdString(dreamscapes::review::Session::keyForEntry(nativeReviewValue(entry)));
}
QVariantList GenerationReviewModel::originalsFor(const std::vector<Value> &values) const {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(values.size()));
    for (const auto &value : values) {
        const auto key = QString::fromStdString(dreamscapes::review::Session::keyForEntry(value));
        const auto found = m_entries.constFind(key);
        result.push_back(found == m_entries.cend() ? qtReviewValue(value) : QVariant(*found));
    }
    return result;
}
QVariantList GenerationReviewModel::entries() const { return originalsFor(m_session->entries()); }
void GenerationReviewModel::setEntries(const QVariantList &entries) {
    if (deferDuringNotification([this, entries] { setEntries(entries); })) return;
    QHash<QString,QVariantMap> normalized;
    std::vector<Value> values;
    values.reserve(static_cast<std::size_t>(entries.size()));
    for (const auto &value : entries) {
        const auto entry = value.toMap();
        const auto native = nativeReviewValue(entry);
        const auto key = QString::fromStdString(dreamscapes::review::Session::keyForEntry(native));
        if (key.isEmpty() || normalized.contains(key)) continue;
        normalized.insert(key, entry);
        values.push_back(native);
    }
    if (normalized == m_entries) return;
    m_rawMetadataChanged.clear();
    for (auto it = normalized.cbegin(); it != normalized.cend(); ++it)
        if (m_entries.value(it.key()) != it.value()) m_rawMetadataChanged.insert(it.key());
    m_entries = std::move(normalized);
    const auto previous = m_notifications;
    m_session->replaceEntries(values);
    // Qt payload types can change while their native projection is identical.
    if (m_notifications == previous)
        applyNativeChange({.previousCount = m_session->visibleCount(), .entriesChanged = true});
}
void GenerationReviewModel::synchronizeOriginals() {
    QHash<QString,QVariantMap> next;
    for (const auto &value : m_session->entries()) {
        const auto key = QString::fromStdString(dreamscapes::review::Session::keyForEntry(value));
        const auto found = m_entries.constFind(key);
        if (found != m_entries.cend() && nativeReviewValue(*found) == value) next.insert(key, *found);
        else next.insert(key, qtReviewValue(value).toMap());
    }
    m_entries = std::move(next);
}
QString GenerationReviewModel::stateName(State state) {
    if (state == State::Shortlisted) return QStringLiteral("shortlisted");
    if (state == State::Rejected) return QStringLiteral("rejected");
    return QStringLiteral("unreviewed");
}
GenerationReviewModel::State GenerationReviewModel::parseState(const QString &state) {
    if (state == "shortlisted") return State::Shortlisted;
    if (state == "rejected") return State::Rejected;
    return State::Unreviewed;
}
void GenerationReviewModel::applyNativeChange(const dreamscapes::review::Change &change) {
    ++m_notifications;
    struct Guard { bool &value; explicit Guard(bool &v) : value(v) { value = true; } ~Guard() { value = false; } } guard(m_notifying);
    if (change.entriesChanged) synchronizeOriginals();
    const auto nextFilter = QString::fromStdString(m_session->filter());
    const auto nextJobFilter = QString::fromStdString(m_session->jobFilter());
    const auto nextSort = QString::fromStdString(m_session->sort());
    const bool filterChangedValue = m_filter != nextFilter || m_jobFilter != nextJobFilter;
    const bool sortChangedValue = m_sort != nextSort;
    m_filter = nextFilter; m_jobFilter = nextJobFilter; m_sort = nextSort;
    if (change.projection == dreamscapes::review::ProjectionChange::None && !change.entriesChanged &&
        m_rawMetadataChanged.isEmpty()) {
        const bool focusOnly = !change.rows.empty() && std::ranges::all_of(change.rows, [](const auto &row) {
            return row.roles == dreamscapes::review::Focused;
        });
        struct ChangedRows { int first, last; QList<int> roles; };
        QList<ChangedRows> changed;
        for (const auto &item : change.rows) {
            const int indexValue = m_visibleIndices.value(QString::fromStdString(item.key), -1);
            if (indexValue < 0) continue;
            const auto row = m_session->rowAt(indexValue);
            if (!row) throw std::logic_error("Native review projection has no changed row");
            const RowState next{row->review, row->selected, row->focused};
            const auto old = m_rowStates[indexValue];
            QList<int> roles;
            if (!(item.roles & dreamscapes::review::Metadata)) {
                if (old.selected != next.selected) roles.append(SelectedRole);
                if (old.focused != next.focused) roles.append(FocusedRole);
                if (old.review != next.review) roles.append(ReviewStateRole);
                if (roles.isEmpty()) continue;
            }
            m_rowStates[indexValue] = next;
            if (!focusOnly && !changed.isEmpty() && changed.last().last + 1 == indexValue && changed.last().roles == roles)
                changed.last().last = indexValue;
            else changed.append({indexValue, indexValue, roles});
        }
        for (const auto &rows : changed) emit dataChanged(index(rows.first), index(rows.last), rows.roles);
        emit stateChanged();
        if (filterChangedValue) emit filterChanged();
        if (sortChangedValue) emit sortChanged();
        return;
    }
    QStringList next;
    QList<RowState> nextStates;
    next.reserve(static_cast<qsizetype>(m_session->visibleCount()));
    nextStates.reserve(static_cast<qsizetype>(m_session->visibleCount()));
    for (std::size_t i = 0; i < m_session->visibleCount(); ++i) {
        const auto row = m_session->rowAt(static_cast<int>(i));
        if (!row) throw std::logic_error("Native review projection has no visible row");
        next.push_back(QString::fromStdString(row->key));
        nextStates.push_back({row->review, row->selected, row->focused});
    }
    m_ordinals.clear();
    int ordinal = 0;
    for (const auto &key : m_session->allKeys()) m_ordinals.insert(QString::fromStdString(key), ++ordinal);
    QSet<QString> metadata = std::move(m_rawMetadataChanged);
    m_rawMetadataChanged.clear();
    for (const auto &row : change.rows)
        if (row.roles & dreamscapes::review::Metadata) metadata.insert(QString::fromStdString(row.key));
    const auto previousStates = m_rowStates;
    const int previousSize = m_visible.size();
    bool reset = false;
    if (next != m_visible) {
        emit projectionAboutToChange();
        const bool append = next.size() > m_visible.size() && std::equal(m_visible.begin(), m_visible.end(), next.begin());
        reset = !append;
        if (append) beginInsertRows({}, m_visible.size(), next.size() - 1);
        else beginResetModel();
        m_visible = next;
        m_visibleIndices.clear();
        for (int i = 0; i < m_visible.size(); ++i) m_visibleIndices.insert(m_visible[i], i);
        if (reset) m_rowStates = nextStates;
        else for (int i = m_rowStates.size(); i < nextStates.size(); ++i) m_rowStates.append(nextStates[i]);
        if (append) endInsertRows(); else endResetModel();
        emit projectionChanged();
    }
    const bool focusOnly = !reset && !change.rows.empty() && metadata.isEmpty() &&
        std::ranges::all_of(change.rows, [](const auto &row) { return row.roles == dreamscapes::review::Focused; });
    struct ChangedRows { int first, last; QList<int> roles; };
    QList<ChangedRows> changed;
    if (!reset) for (int i = 0; i < previousSize; ++i) {
        const auto current = nextStates[i], old = previousStates[i];
        QList<int> roles;
        if (!metadata.contains(m_visible[i])) {
            if (old.selected != current.selected) roles.append(SelectedRole);
            if (old.focused != current.focused) roles.append(FocusedRole);
            if (old.review != current.review) roles.append(ReviewStateRole);
            if (roles.isEmpty()) continue;
        }
        m_rowStates[i] = current;
        if (!focusOnly && !changed.isEmpty() && changed.last().last + 1 == i && changed.last().roles == roles)
            changed.last().last = i;
        else changed.append({i, i, roles});
    }
    for (const auto &rows : changed) emit dataChanged(index(rows.first), index(rows.last), rows.roles);
    emit stateChanged();
    if (change.entriesChanged) emit entriesChanged();
    if (filterChangedValue) emit filterChanged();
    if (sortChangedValue) emit sortChanged();
}
void GenerationReviewModel::setFilter(const QString &value) {
    if (deferDuringNotification([this, value] { setFilter(value); })) return;
    m_session->setFilter(value.toStdString());
}
void GenerationReviewModel::setSort(const QString &value) {
    if (deferDuringNotification([this, value] { setSort(value); })) return;
    m_session->setSort(value.toStdString());
}
void GenerationReviewModel::setJobFilter(const QString &value) {
    if (deferDuringNotification([this, value] { setJobFilter(value); })) return;
    m_session->setJobFilter(value.toStdString());
}
void GenerationReviewModel::setSelectionMode(bool value) {
    if (deferDuringNotification([this, value] { setSelectionMode(value); })) return;
    m_session->setSelectionMode(value);
}
int GenerationReviewModel::shortlistedCount() const { return int(m_session->shortlistedCount()); }
int GenerationReviewModel::rejectedCount() const { return int(m_session->rejectedCount()); }
int GenerationReviewModel::hiddenSelectedCount() const { return int(m_session->hiddenSelectedCount()); }
QVariantList GenerationReviewModel::selectedEntries() const { return originalsFor(m_session->selectedEntries()); }
QVariantList GenerationReviewModel::selectedImages() const { return originalsFor(m_session->selectedImages()); }
QVariantList GenerationReviewModel::actionEntries() const { return originalsFor(m_session->actionEntries()); }
QString GenerationReviewModel::focusedKey() const { return QString::fromStdString(m_session->focusedKey()); }
int GenerationReviewModel::focusedIndex() const { return indexForKey(focusedKey()); }
QVariantMap GenerationReviewModel::focusedEntry() const { return m_entries.value(focusedKey()); }
QVariantMap GenerationReviewModel::entryAt(int row) const { return row >= 0 && row < m_visible.size() ? m_entries.value(m_visible[row]) : QVariantMap{}; }
int GenerationReviewModel::indexForKey(const QString &key) const { return m_visibleIndices.value(key, -1); }
bool GenerationReviewModel::isSelected(const QVariantMap &entry) const { return m_session->isSelected(nativeReviewValue(entry)); }
void GenerationReviewModel::selectAt(int row, int modifiers) {
    if (deferDuringNotification([this, row, modifiers] { selectAt(row, modifiers); })) return;
    m_session->selectAt(row, bool(modifiers & Qt::ShiftModifier), bool(modifiers & (Qt::ControlModifier | Qt::MetaModifier)));
}
void GenerationReviewModel::focusAt(int row) {
    if (deferDuringNotification([this, row] { focusAt(row); })) return;
    m_session->focusAt(row);
}
void GenerationReviewModel::focusSource(const QString &source) {
    if (deferDuringNotification([this, source] { focusSource(source); })) return;
    m_session->focusSource(source.toStdString());
}
void GenerationReviewModel::toggle(const QVariantMap &entry) {
    if (deferDuringNotification([this, entry] { toggle(entry); })) return;
    m_session->toggle(nativeReviewValue(entry));
}
void GenerationReviewModel::selectAll() {
    if (deferDuringNotification([this] { selectAll(); })) return;
    m_session->selectAll();
}
void GenerationReviewModel::clearSelection() {
    if (deferDuringNotification([this] { clearSelection(); })) return;
    m_session->clearSelection();
}
void GenerationReviewModel::toggleReview(const QString &state) {
    if (deferDuringNotification([this, state] { toggleReview(state); })) return;
    m_session->toggleReview(parseState(state));
}
void GenerationReviewModel::setReview(const QString &state) {
    if (deferDuringNotification([this, state] { setReview(state); })) return;
    m_session->setReview(parseState(state));
}
void GenerationReviewModel::setEntryReview(const QString &key, const QString &state) {
    if (deferDuringNotification([this, key, state] { setEntryReview(key, state); })) return;
    m_session->setEntryReview(key.toStdString(), parseState(state));
}
QString GenerationReviewModel::reviewState(const QString &key) const { return stateName(m_session->reviewState(key.toStdString())); }

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
    if (m_notifying) return {};
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
    input.overlay = overlay; input.detail = detail; input.compare = compare; input.columns = columns;
    const auto reply = m_session->key(input, false);
    if (!reply.accepted) return {};
    QString action;
    switch (reply.action) {
    case ReviewAction::back: action = "back"; break;
    case ReviewAction::compare: action = "compare"; break;
    case ReviewAction::detail: action = "detail"; break;
    case ReviewAction::edit: action = "edit"; break;
    case ReviewAction::focus: action = "focus"; break;
    default: break;
    }
    return {{"accepted", true}, {"action", action}, {"index", reply.index}};
}
bool GenerationReviewModel::validImages(const QVariantList &entries, int minimum, int maximum) const {
    return dreamscapes::review::Session::validImages(nativeReviewValues(entries), minimum, maximum);
}
void GenerationReviewModel::markEntries(const QVariantList &entries, const QString &state) {
    if (deferDuringNotification([this, entries, state] { markEntries(entries, state); })) return;
    m_session->markEntries(nativeReviewValues(entries), parseState(state));
}
QVariantList GenerationReviewModel::contextEntries() const {
    return isSelected(focusedEntry()) ? selectedEntries() : QVariantList{focusedEntry()};
}

QString GenerationReviewModel::filterLabel(const QString &key) const {
    if (key == "all") return tr("All %1").arg(totalCount());
    if (key == "shortlisted") return tr("Shortlisted %1").arg(shortlistedCount());
    if (key == "rejected") return tr("Rejected %1").arg(rejectedCount());
    if (key == "unreviewed") return tr("Unreviewed");
    if (key == "images") return tr("Images");
    if (key == "videos") return tr("Videos");
    return key;
}
