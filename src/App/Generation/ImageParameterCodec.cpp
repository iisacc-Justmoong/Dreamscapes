#include "ImageParameterCodec.h"
#include <cmath>
#include <type_traits>

namespace dreamscapes {
using namespace iiLocalDiffusion;
namespace {
QVariant valueToVariant(const ImageParameterValue &v)
{
    return std::visit([](const auto &value) -> QVariant {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::string>) return QString::fromStdString(value);
        else if constexpr (std::is_same_v<T, std::int64_t>) return QVariant::fromValue(qint64(value));
        else return QVariant::fromValue(value);
    }, v);
}
bool convert(const QVariant &v, ImageParameterValue *out)
{
    const auto type = v.metaType().id();
    if (std::holds_alternative<std::string>(*out)) {
        if (type != QMetaType::QString) return false;
        *out = v.toString().toStdString(); return true;
    }
    if (std::holds_alternative<bool>(*out)) {
        if (type != QMetaType::Bool) return false;
        *out = v.toBool(); return true;
    }
    if (type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong
        && type != QMetaType::ULongLong && type != QMetaType::Double && type != QMetaType::Float) return false;
    const auto number = v.toDouble();
    if (!std::isfinite(number)) return false;
    if (std::holds_alternative<std::int64_t>(*out)) {
        // All public integer ranges fit exactly in the QML/JSON double range.
        if (std::trunc(number) != number || number < -1 || number > 4294967295.0) return false;
        *out = std::int64_t(number);
    } else *out = number;
    return true;
}
template<typename Setter>
bool collection(const QVariant &value, const std::map<std::string, ImageParameterValue> &defaults,
    Setter append, QString *error)
{
    if (value.metaType().id() != QMetaType::QVariantList) { *error = "Expected an array."; return false; }
    for (const auto &item : value.toList()) {
        if (item.metaType().id() != QMetaType::QVariantMap) { *error = "Expected a collection object."; return false; }
        auto fields = defaults;
        const auto map = item.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            const auto field = fields.find(it.key().toStdString());
            if (field == fields.end() || !convert(it.value(), &field->second)) {
                *error = "Unknown collection field or incorrect type: " + it.key(); return false;
            }
        }
        append(fields);
    }
    return true;
}
}
QVariantMap imageParametersToMap(const ImageParameters &p)
{
    QVariantMap result;
    for (const auto &[key, value] : p.values) result.insert(QString::fromStdString(key), valueToVariant(value));
    QVariantList references, controls, loras;
    for (const auto &source : p.references) references.append(QString::fromStdString(source));
    for (const auto &c : p.controls) controls.append(QVariantMap{
        {"id", QString::fromStdString(c.id)}, {"process", QString::fromStdString(c.process)},
        {"imageSource", QString::fromStdString(c.imageSource)}, {"model", QString::fromStdString(c.model)},
        {"maskSource", QString::fromStdString(c.maskSource)}, {"weight", c.weight},
        {"ipAdapter", c.ipAdapter}, {"regionalMask", c.regionalMask}, {"applied", c.applied},
        {"ipAdapterModel", QString::fromStdString(c.ipAdapterModel)}, {"ipAdapterVision", QString::fromStdString(c.ipAdapterVision)},
        {"poseDetector", QString::fromStdString(c.poseDetector)}, {"poseModel", QString::fromStdString(c.poseModel)}});
    for (const auto &l : p.loras) loras.append(QVariantMap{{"id", QString::fromStdString(l.id)},
        {"source", QString::fromStdString(l.source)}, {"name", QString::fromStdString(l.name)}, {"weight", l.weight}});
    result.insert("referenceImages", references); result.insert("controlNets", controls); result.insert("loras", loras);
    return result;
}
bool imageParametersFromMap(const QVariantMap &map, ImageParameters *parameters, QString *error)
{
    auto next = ImageParameters::defaults();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.key() == "referenceImages" || it.key() == "controlNets" || it.key() == "loras") continue;
        const auto field = next.values.find(it.key().toStdString());
        if (field == next.values.end() || !convert(it.value(), &field->second)) {
            *error = "Unknown parameter or incorrect type: " + it.key(); return false;
        }
    }
    if (map.contains("referenceImages")) {
        if (map.value("referenceImages").metaType().id() != QMetaType::QVariantList) { *error = "Expected referenceImages array."; return false; }
        for (const auto &v : map.value("referenceImages").toList()) {
            if (v.metaType().id() != QMetaType::QString) { *error = "Expected an image source string."; return false; }
            next.references.push_back(v.toString().toStdString());
        }
    }
    const auto string = std::string{};
    if (map.contains("controlNets") && !collection(map.value("controlNets"),
        {{"id", string}, {"process", std::string("None")}, {"imageSource", string}, {"model", string},
         {"maskSource", string}, {"weight", 1.0}, {"ipAdapter", false}, {"regionalMask", false}, {"applied", false},
         {"ipAdapterModel", string}, {"ipAdapterVision", string}, {"poseDetector", string}, {"poseModel", string}},
        [&](const auto &f) { next.controls.push_back({std::get<std::string>(f.at("id")), std::get<std::string>(f.at("process")),
            std::get<std::string>(f.at("imageSource")), std::get<std::string>(f.at("model")), std::get<std::string>(f.at("maskSource")),
            std::get<double>(f.at("weight")), std::get<bool>(f.at("ipAdapter")), std::get<bool>(f.at("regionalMask")),
            std::get<bool>(f.at("applied")), std::get<std::string>(f.at("ipAdapterModel")),
            std::get<std::string>(f.at("ipAdapterVision")), std::get<std::string>(f.at("poseDetector")),
            std::get<std::string>(f.at("poseModel"))}); }, error)) return false;
    if (map.contains("loras") && !collection(map.value("loras"),
        {{"id", string}, {"source", string}, {"name", string}, {"weight", 1.0}},
        [&](const auto &f) { next.loras.push_back({std::get<std::string>(f.at("id")), std::get<std::string>(f.at("source")),
            std::get<std::string>(f.at("name")), std::get<double>(f.at("weight"))}); }, error)) return false;
    const auto issues = validateImageParameters(next);
    if (!issues.empty()) { *error = QString::fromStdString(issues.front().field + ": " + issues.front().message); return false; }
    *parameters = std::move(next); error->clear(); return true;
}
QVariantList imageParameterIssues(const std::vector<ImageParameterIssue> &issues)
{
    QVariantList result;
    for (const auto &issue : issues) result.append(QVariantMap{{"field", QString::fromStdString(issue.field)},
        {"message", QString::fromStdString(issue.message)}});
    return result;
}
QVariantList imageParameterSchema()
{
    QVariantList result;
    for (const auto &s : imageParameterSpecs()) {
        QStringList choices; for (const auto &v : s.choices) choices.append(QString::fromStdString(v));
        result.append(QVariantMap{{"key", QString::fromStdString(s.key)}, {"section", QString::fromStdString(s.section)},
            {"defaultValue", valueToVariant(s.initial)}, {"minimum", s.minimum}, {"maximum", s.maximum},
            {"choices", choices}, {"type", QStringList{"bool", "integer", "number", "string"}.at(s.initial.index())}});
    }
    return result;
}
}
