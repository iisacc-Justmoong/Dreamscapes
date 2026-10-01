#include "AdvancedImageParameters.h"
#include <Generation/NativePose.hpp>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

AdvancedImageParameters::AdvancedImageParameters(QObject *parent)
    : AdvancedImageParameters(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/advanced-image-presets.json", parent) {}
AdvancedImageParameters::AdvancedImageParameters(QString presetFile, QObject *parent)
    : QObject(parent), m_presetFile(std::move(presetFile)) { readPresets(&m_presets); }
QVariantMap AdvancedImageParameters::parameters() const { return dreamscapes::imageParametersToMap(m_parameters); }
QVariantList AdvancedImageParameters::schema() const { return dreamscapes::imageParameterSchema(); }
QStringList AdvancedImageParameters::presets() const { return m_presets.keys(); }
QString AdvancedImageParameters::errorString() const { return m_error; }
bool AdvancedImageParameters::fail(const QString &error)
{
    if (m_error != error) { m_error = error; emit errorChanged(); }
    return error.isEmpty();
}
bool AdvancedImageParameters::updateParameters(const QVariantMap &patch)
{
    auto map = parameters();
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) map.insert(it.key(), it.value());
    iiLocalDiffusion::ImageParameters next;
    QString error;
    if (!dreamscapes::imageParametersFromMap(map, &next, &error)) return fail(error);
    if (m_parameters != next) { m_parameters = std::move(next); emit parametersChanged(); }
    return fail({});
}
void AdvancedImageParameters::reset()
{
    updateParameters(dreamscapes::imageParametersToMap(iiLocalDiffusion::ImageParameters::defaults()));
}
QVariantList AdvancedImageParameters::submissionIssues(bool desktopWorker) const
{
    auto issues = iiLocalDiffusion::validateImageParameters(m_parameters, true);
    const auto unsupported = iiLocalDiffusion::nativeParameterIssues(m_parameters, desktopWorker, iiLocalDiffusion::nativePoseAvailable());
    issues.insert(issues.end(), unsupported.begin(), unsupported.end());
    return dreamscapes::imageParameterIssues(issues);
}
bool AdvancedImageParameters::addReferenceImage(const QString &source)
{
    auto values = parameters().value("referenceImages").toList(); values.append(source);
    return updateParameters({{"referenceImages", values}});
}
bool AdvancedImageParameters::removeReferenceImage(int index)
{
    auto values = parameters().value("referenceImages").toList();
    if (index < 0 || index >= values.size()) return fail("Reference index is out of range.");
    values.removeAt(index); return updateParameters({{"referenceImages", values}});
}
QString AdvancedImageParameters::addControlNet(const QString &process)
{
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto values = parameters().value("controlNets").toList();
    values.append(QVariantMap{{"id", id}, {"process", process}});
    return updateParameters({{"controlNets", values}}) ? id : QString{};
}
bool AdvancedImageParameters::updateItem(const QString &collection, const QString &id, const QVariantMap &patch, bool remove)
{
    if (patch.contains("id")) return fail("Collection IDs cannot be changed.");
    auto values = parameters().value(collection).toList();
    for (qsizetype i = 0; i < values.size(); ++i) {
        auto item = values[i].toMap();
        if (item.value("id").toString() != id) continue;
        if (remove) values.removeAt(i);
        else {
            for (auto it = patch.cbegin(); it != patch.cend(); ++it) item.insert(it.key(), it.value());
            values[i] = item;
        }
        return updateParameters({{collection, values}});
    }
    return fail("Collection item does not exist: " + id);
}
bool AdvancedImageParameters::updateControlNet(const QString &id, const QVariantMap &patch)
{
    if (patch.contains("applied")) return fail("Use applyControlNet to apply a draft.");
    if (patch.contains("id")) return fail("Collection IDs cannot be changed.");
    auto map = parameters();
    auto controls = map.value("controlNets").toList();
    for (qsizetype i = 0; i < controls.size(); ++i) {
        auto item = controls[i].toMap();
        if (item.value("id").toString() != id) continue;
        for (auto it = patch.cbegin(); it != patch.cend(); ++it) item.insert(it.key(), it.value());
        // Validate as a draft so removing a required source remains an editable
        // operation. Compare typed values, not QVariant's permissive conversions.
        item.insert("applied", false);
        controls[i] = item;
        map.insert("controlNets", controls);
        iiLocalDiffusion::ImageParameters next;
        QString error;
        if (!dreamscapes::imageParametersFromMap(map, &next, &error)) return fail(error);
        auto &control = next.controls[static_cast<size_t>(i)];
        control.applied = m_parameters.controls[static_cast<size_t>(i)].applied;
        if (next == m_parameters) return fail({});
        control.applied = false;
        m_parameters = std::move(next);
        emit parametersChanged();
        return fail({});
    }
    return fail("Collection item does not exist: " + id);
}
bool AdvancedImageParameters::applyControlNet(const QString &id) { return updateItem("controlNets", id, {{"applied", true}}); }
bool AdvancedImageParameters::resetControlNet(const QString &id)
{
    return updateItem("controlNets", id, {{"process", "None"}, {"imageSource", ""}, {"model", ""},
        {"maskSource", ""}, {"weight", 1.0}, {"ipAdapter", false}, {"regionalMask", false}, {"applied", false},
        {"ipAdapterModel", ""}, {"ipAdapterVision", ""}, {"poseDetector", ""}, {"poseModel", ""}});
}
bool AdvancedImageParameters::removeControlNet(const QString &id) { return updateItem("controlNets", id, {}, true); }
QString AdvancedImageParameters::addLora(const QString &source, const QString &name)
{
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto values = parameters().value("loras").toList();
    values.append(QVariantMap{{"id", id}, {"source", source}, {"name", name}, {"weight", 1.0}});
    return updateParameters({{"loras", values}}) ? id : QString{};
}
bool AdvancedImageParameters::updateLora(const QString &id, const QVariantMap &patch) { return updateItem("loras", id, patch); }
bool AdvancedImageParameters::removeLora(const QString &id) { return updateItem("loras", id, {}, true); }

bool AdvancedImageParameters::readPresets(QVariantMap *presets)
{
    QFile file(m_presetFile);
    if (!file.exists()) { presets->clear(); return true; }
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    if (file.size() > 8 * 1024 * 1024) return fail("Preset file exceeds 8 MiB.");
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto object = doc.object();
    if (parse.error != QJsonParseError::NoError || object.value("schemaVersion").toInt() != 1
        || !object.value("presets").isObject()) return fail("Invalid or unsupported preset file.");
    const auto next = object.value("presets").toObject().toVariantMap();
    if (next.size() > 100) return fail("At most 100 presets are allowed.");
    for (auto it = next.cbegin(); it != next.cend(); ++it) {
        iiLocalDiffusion::ImageParameters parameters;
        QString error;
        if (it.key().trimmed().isEmpty() || it.key().size() > 100 || it.value().metaType().id() != QMetaType::QVariantMap
            || !dreamscapes::imageParametersFromMap(it.value().toMap(), &parameters, &error))
            return fail("Invalid preset: " + it.key() + ". " + error);
    }
    *presets = next; return true;
}
bool AdvancedImageParameters::writePresets(const QVariantMap &presets)
{
    QSaveFile file(m_presetFile);
    const auto bytes = QJsonDocument(QJsonObject{{"schemaVersion", 1},
        {"presets", QJsonObject::fromVariantMap(presets)}}).toJson();
    if (bytes.size() > 8 * 1024 * 1024) return fail("Preset file exceeds 8 MiB.");
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return fail(file.errorString());
    if (m_presets != presets) { m_presets = presets; emit presetsChanged(); }
    return fail({});
}
bool AdvancedImageParameters::savePreset(const QString &name)
{
    const auto key = name.trimmed();
    if (key.isEmpty() || key.size() > 100 || key.contains(QChar::Null)) return fail("Preset name must contain 1 to 100 characters.");
    if (!QDir().mkpath(QFileInfo(m_presetFile).absolutePath())) return fail("Cannot create the preset directory.");
    QLockFile lock(m_presetFile + ".lock");
    if (!lock.tryLock(0)) return fail("Presets are being edited by another process.");
    QVariantMap next;
    if (!readPresets(&next)) return false;
    next.insert(key, parameters());
    if (next.size() > 100) return fail("At most 100 presets are allowed.");
    return writePresets(next);
}
bool AdvancedImageParameters::loadPreset(const QString &name)
{
    QVariantMap next;
    if (!readPresets(&next)) return false;
    const auto key = name.trimmed();
    if (!next.contains(key)) return fail("Preset does not exist: " + key);
    iiLocalDiffusion::ImageParameters restored;
    QString error;
    if (!dreamscapes::imageParametersFromMap(next.value(key).toMap(), &restored, &error)) return fail(error);
    if (!updateParameters(dreamscapes::imageParametersToMap(restored))) return false;
    if (m_presets != next) { m_presets = next; emit presetsChanged(); }
    return true;
}
bool AdvancedImageParameters::removePreset(const QString &name)
{
    QLockFile lock(m_presetFile + ".lock");
    if (!lock.tryLock(0)) return fail("Cannot lock the preset file.");
    QVariantMap next;
    if (!readPresets(&next)) return false;
    if (!next.remove(name.trimmed())) return fail("Preset does not exist: " + name.trimmed());
    return writePresets(next);
}
