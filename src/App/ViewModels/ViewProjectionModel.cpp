#include "ViewProjectionModel.h"
#include "ViewData.h"

namespace dreamscapes {
QVariant ViewProjectionModel::read(vm::ProjectionRead operation, std::initializer_list<QVariant> arguments) const {
    vm::ProjectionQuery query{operation, {}};
    for (const auto &argument : arguments) query.arguments.push_back(nativeValue(argument));
    auto reply = projection_.queries().send(query);
    return reply ? viewValue(*reply) : QVariant{};
}
QStringList ViewProjectionModel::labels(const QVariantList &rows, const QString &key, const QString &fallback, int selected) const {
    return read(vm::ProjectionRead::labels, {rows, key, fallback, selected}).toStringList();
}
QVariantList ViewProjectionModel::values(const QVariantList &rows, const QString &key) const {
    return read(vm::ProjectionRead::values, {rows, key}).toList();
}
QVariantList ViewProjectionModel::choices(const QVariantList &rows, const QString &labelKey, const QString &valueKey,
                                         const QString &valueRole, const QString &fallback) const {
    return read(vm::ProjectionRead::choices, {rows, labelKey, fallback, valueKey, valueRole}).toList();
}
int ViewProjectionModel::indexOf(const QVariantList &rows, const QVariant &value, const QString &key) const {
    const auto result = read(vm::ProjectionRead::index, {rows, value, key});
    return result.isValid() ? result.toInt() : -1;
}
QVariantList ViewProjectionModel::matching(const QVariantList &rows, const QVariant &value, const QString &key) const {
    QVariantList result;
    for (const auto &index : read(vm::ProjectionRead::matchingIndices, {rows, value, key}).toList())
        if (index.toInt() >= 0 && index.toInt() < rows.size()) result.append(rows[index.toInt()]);
    return result;
}
QVariantMap ViewProjectionModel::find(const QVariantList &rows, const QVariant &value, const QString &key) const {
    const int index = indexOf(rows, value, key);
    return index >= 0 && index < rows.size() ? rows[index].toMap() : QVariantMap{};
}
QVariantList ViewProjectionModel::range(const QVariantList &rows, int begin, int end) const {
    QVariantList result;
    for (const auto &index : read(vm::ProjectionRead::indices, {rows.size(), begin, end}).toList())
        result.append(rows.at(index.toInt()));
    return result;
}
QVariantList ViewProjectionModel::concatenate(const QVariantList &first, const QVariantList &second) const {
    QVariantList result;
    for (const auto &index : read(vm::ProjectionRead::concatenateIndices, {first.size(), second.size()}).toList()) {
        const int i = index.toInt();
        result.append(i < first.size() ? first.at(i) : second.at(i - first.size()));
    }
    return result;
}
QVariantList ViewProjectionModel::prepend(const QVariant &entry, const QVariantList &rows) const {
    return concatenate({entry}, rows);
}
QString ViewProjectionModel::join(const QVariantList &rows, const QString &separator) const {
    return read(vm::ProjectionRead::join, {rows, separator}).toString();
}
QVariantList ViewProjectionModel::countChoices(int current) const {
    return read(vm::ProjectionRead::counts, {current}).toList();
}
QStringList ViewProjectionModel::videoCountLabels(const QVariantList &rows, const QString &custom) const {
    QStringList result;
    for (const auto &count : rows) result.append(tr("%1 video%2").arg(count.toInt()).arg(count.toInt() == 1 ? "" : "s"));
    result.append(custom);
    return result;
}
QStringList ViewProjectionModel::notifications(const QVariantList &rows) const {
    const QVariantMap states{{"queued", tr("Queued")}, {"running", tr("Generating")}, {"completed", tr("Completed")},
        {"failed", tr("Failed")}, {"cancelled", tr("Cancelled")}, {"interrupted", tr("Interrupted")},
        {"downloading", tr("Downloading")}, {"connecting-host", tr("Connecting")}};
    return read(vm::ProjectionRead::notifications, {rows, states, tr("Generation"), tr("No generation notifications")}).toStringList();
}
QVariantMap ViewProjectionModel::timelineLayout(const QVariantList &rows, const QString &mode) const {
    auto result = read(vm::ProjectionRead::timelineLayout, {rows, mode}).toMap();
    auto layout = result.value("tracks").toList();
    for (qsizetype i = 0; i < layout.size(); ++i) {
        const auto geometry = layout[i].toMap();
        auto original = rows.at(i).toMap();
        original.insert("top", geometry.value("top")); original.insert("height", geometry.value("height"));
        layout[i] = original;
    }
    result.insert("tracks", layout);
    return result;
}
QString ViewProjectionModel::timecode(int frame, double fps) const {
    return read(vm::ProjectionRead::timecode, {frame, fps}).toString();
}
QStringList ViewProjectionModel::trackLabels(const QVariantList &rows) const {
    return read(vm::ProjectionRead::trackLabels, {rows}).toStringList();
}
QStringList ViewProjectionModel::videoResultLabels(const QVariantList &rows) const {
    QStringList result;
    for (const auto &entry : read(vm::ProjectionRead::videoResults, {rows}).toList()) {
        const auto row = entry.toMap();
        result.append(tr("Video %1 · %2 frames").arg(row.value("ordinal").toInt()).arg(row.value("frames", 0).toInt()));
    }
    return result;
}
QStringList ViewProjectionModel::sizeChoices(const QVariantList &rows, const QString &ratio) const {
    return read(vm::ProjectionRead::sizeChoices, {rows, ratio}).toStringList();
}
QVariant ViewProjectionModel::nextChoice(const QVariantList &rows, const QVariant &value) const {
    return read(vm::ProjectionRead::nextChoice, {rows, value});
}
QStringList ViewProjectionModel::filenames(const QVariantList &rows) const {
    return read(vm::ProjectionRead::filenames, {rows}).toStringList();
}
int ViewProjectionModel::rulerStep(double pitch) const {
    return read(vm::ProjectionRead::rulerStep, {pitch}).toInt();
}
QString ViewProjectionModel::fileField(const QVariantMap &view, const QVariantMap &recorded, const QString &key,
                                      const QString &unavailable) const {
    return read(vm::ProjectionRead::fileField, {view, recorded, key, unavailable}).toString();
}
} // namespace dreamscapes
