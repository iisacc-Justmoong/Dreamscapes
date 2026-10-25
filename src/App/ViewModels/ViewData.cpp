#include "ViewData.h"
#include "VideoTimelineModel.h"
#include "GenerationProjection.h"
#include <iiJson/Json.h>
#include <QDateTime>
#include <QFile>
#include <QJSValue>
#include <QQmlEngine>

static void initializeNativeViewCatalogs() { Q_INIT_RESOURCE(native_view_catalogs); }
namespace dreamscapes {
using namespace vm;
Value nativeValue(const QVariant &input) {
    const auto value = input.metaType() == QMetaType::fromType<QJSValue>() ? input.value<QJSValue>().toVariant() : input;
    if (!value.isValid() || value.isNull()) return {};
    switch (value.metaType().id()) {
    case QMetaType::Bool: return Value(value.toBool());
    case QMetaType::Int: case QMetaType::LongLong: case QMetaType::Long: case QMetaType::Short: return Value(value.toLongLong());
    case QMetaType::UInt: case QMetaType::ULongLong: case QMetaType::ULong: case QMetaType::UShort: return Value(value.toULongLong());
    case QMetaType::Float: case QMetaType::Double: return std::isfinite(value.toDouble()) ? Value(value.toDouble()) : Value();
    case QMetaType::QVariantMap: { Object result; const auto map = value.toMap(); for (auto it = map.begin(); it != map.end(); ++it) result[it.key().toStdString()] = nativeValue(it.value()); return Value(std::move(result)); }
    case QMetaType::QStringList: case QMetaType::QVariantList: { Array result; for (const auto &item : value.toList()) result.push_back(nativeValue(item)); return Value(std::move(result)); }
    default: return Value(value.toString().toStdString());
    }
}
QVariant viewValue(const Value &value) {
    return std::visit([](const auto &v) -> QVariant {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::same_as<T, std::monostate>) return QVariant::fromValue(nullptr);
        else if constexpr (std::same_as<T, std::string>) return QString::fromStdString(v);
        else if constexpr (std::same_as<T, Object>) { QVariantMap result; for (const auto &[key, item] : v) result[QString::fromStdString(key)] = viewValue(item); return result; }
        else if constexpr (std::same_as<T, Array>) { QVariantList result; result.reserve(v.size()); for (const auto &item : v) result.push_back(viewValue(item)); return result; }
        else if constexpr (std::same_as<T, ii::core::Bytes>) return QByteArray(reinterpret_cast<const char *>(v.data()), v.size());
        else return QVariant::fromValue(v);
    }, value.storage());
}
ViewData::ViewData(QObject *parent) : QObject(parent) {
    initializeNativeViewCatalogs();
    std::map<std::string, Value, std::less<>> resources;
    for (auto name : {"tools", "text", "text-definition", "photo", "file", "compact", "elements", "graphic", "view-options", "field-groups"}) {
        QFile file(QStringLiteral(":/viewmodels/%1.json").arg(name));
        if (!file.open(QIODevice::ReadOnly)) qFatal("Missing native view catalog: %s", name);
        const auto bytes = file.readAll();
        ii::json::Limits limits;
        limits.items = 500000; // The bundled 381-panel catalog is bounded and schema checked by tests.
        auto parsed = ii::json::parse(std::string_view(bytes.constData(), bytes.size()), limits);
        if (!parsed) qFatal("Invalid native view catalog %s: %s", name, parsed.error().message.c_str());
        resources[name] = std::move(*parsed);
    }
    catalog_ = std::make_unique<ViewCatalog>(std::move(resources));
#define VIEW_DATA_CREATE(name) name##_ = new ViewDataDomain(*catalog_, Domain::name, this);
    VIEW_DATA_CREATE(tools) VIEW_DATA_CREATE(graphics) VIEW_DATA_CREATE(graphicState)
    VIEW_DATA_CREATE(file) VIEW_DATA_CREATE(elements) VIEW_DATA_CREATE(text) VIEW_DATA_CREATE(photo)
    VIEW_DATA_CREATE(compact) VIEW_DATA_CREATE(viewOptions) VIEW_DATA_CREATE(selection) VIEW_DATA_CREATE(storage) VIEW_DATA_CREATE(facts)
#undef VIEW_DATA_CREATE
}
ViewDataDomain::ViewDataDomain(ViewCatalog &catalog, Domain domain, QObject *parent) : QObject(parent), catalog_(catalog), domain_(domain) {}
QVariant ViewDataDomain::read(Read operation, std::initializer_list<QVariant> args) const {
    ViewRead request{operation, {}}; for (const auto &arg : args) request.arguments.push_back(nativeValue(arg));
    const auto result = catalog_.queries(domain_).send(request);
    return result ? viewValue(*result) : QVariant();
}
QVariantList ViewDataDomain::viewsData() const { return read(Read::views, {}).toList(); }
#define TEXT_DATA(name) QVariantList ViewDataDomain::name() const { return viewValue(get(catalog_.resource("text"), #name)).toList(); }
TEXT_DATA(roles) TEXT_DATA(aliases) TEXT_DATA(usages) TEXT_DATA(typography) TEXT_DATA(layout) TEXT_DATA(appearance)
#undef TEXT_DATA
#define READ_ONE(name, op, type) QVariant ViewDataDomain::name(const type &value) const { return read(Read::op, {value}); }
READ_ONE(tool, tool, QString) READ_ONE(definition, definition, QString) READ_ONE(viewsFor, views, QString)
READ_ONE(field, field, QString) READ_ONE(defaults, defaults, QVariant) READ_ONE(runtimeDefinition, runtimeDefinition, QVariant)
READ_ONE(normalizedTool, normalizedTool, QString) READ_ONE(panelsFor, panelsFor, QString) READ_ONE(byId, byId, QString)
READ_ONE(overviewFor, overviewFor, QString) READ_ONE(defaultsFor, defaultsFor, QVariant) READ_ONE(operationDefaults, operationDefaults, QString)
READ_ONE(shapeView, shapeView, QString) READ_ONE(roleValues, roleValues, QVariant) READ_ONE(readout, readout, QVariant)
READ_ONE(source, source, QVariant) READ_ONE(key, key, QVariant) READ_ONE(historyImages, historyImages, QVariant)
READ_ONE(kind, kind, QVariant) READ_ONE(dimensions, dimensions, QVariant) READ_ONE(structure, structure, QVariant)
READ_ONE(state, state, QVariant) READ_ONE(model, model, QVariant) READ_ONE(resultSummary, resultSummary, QVariant)
#undef READ_ONE
QVariant ViewDataDomain::catalog() const { return read(Read::catalog, {}); }
QVariant ViewDataDomain::view(const QString &a, const QString &b) const { return read(Read::view, {a, b}); }
QVariant ViewDataDomain::applicableFields(const QVariant &a, const QVariant &b) const { return read(Read::applicableFields, {a,b}); }
QVariant ViewDataDomain::rows(const QVariant &a, bool b) const { return read(Read::rows, {a,b}); }
QVariant ViewDataDomain::controls(const QString &a, const QString &b) const { return read(Read::controls, {a,b}); }
QVariant ViewDataDomain::fillColor(const QString &a, const QString &b) const { return read(Read::fillColor, {a,b}); }
QVariant ViewDataDomain::sections(const QString &a, const QVariant &b) const { return read(Read::sections, {a,b}); }
bool ViewDataDomain::isImage(const QVariant &a) const { return read(Read::isImage, {a}).toBool(); }
QVariant ViewDataDomain::date(const QVariant &a) const {
    const auto map = viewValue(nativeValue(a)).toMap();
    if (!map.value("dateText").toString().isEmpty()) return map.value("dateText");
    const auto d = QDateTime::fromString(map.value("modified").toString(), Qt::ISODateWithMs);
    return d.isValid() ? d.toLocalTime().toString("yyyy-MM-dd") : QStringLiteral("Unavailable");
}
QVariant ViewDataDomain::value(const QVariant &a, const QString &key) const {
    const auto data = nativeValue(a); const auto name = key.toStdString();
    if (has(data, name) && get(data, name).kind() != ii::core::ValueKind::null) return viewValue(get(data, name));
    for (auto parent : {"generationParameters", "parameters", "metadata"}) if (has(data, parent)) return has(get(data, parent), name) ? viewValue(get(get(data, parent), name)) : QVariant();
    return {};
}
QVariant ViewDataDomain::sectionFiles(const QVariant &a, const QString &b) const { return read(Read::sectionFiles, {a,b}); }
QVariant ViewDataDomain::listing(const QVariant &a, const QString &b, const QString &c, const QString &d, const QString &e) const { return read(Read::listing, {a,b,c,d,e}); }
QVariant ViewDataDomain::clone(const QVariant &a) const { return a.isValid() ? viewValue(nativeValue(a)) : QVariant(); }
QStringList ViewDataDomain::pathParts(const QString &path) const { QStringList result; for (const auto &p : vm::pathParts(path.toStdString())) result.push_back(QString::fromStdString(p)); return result; }
QVariant ViewDataDomain::getPath(const QVariant &a, const QString &b) const { const auto result = pathValue(nativeValue(a), b.toStdString()); return result ? viewValue(*result) : QVariant(); }
QVariant ViewDataDomain::withPath(const QVariant &a, const QString &b, const QVariant &c) const { return viewValue(vm::withPath(nativeValue(a), b.toStdString(), nativeValue(c))); }
QVariant ViewDataDomain::nested(const QVariant &a, const QVariant &b) const { return viewValue(vm::nested(nativeValue(a), nativeValue(b))); }
QVariant ViewDataDomain::flatten(const QVariant &a, const QString &b) const { return viewValue(vm::flatten(nativeValue(a), b.toStdString())); }
QString ViewDataDomain::resolvedPath(const QString &a, const QVariant &b) const { return QString::fromStdString(vm::resolvedPath(a.toStdString(), nativeValue(b))); }
QString ViewDataDomain::numberText(const QVariant &a, const QVariant &b, bool desktop) const { return QString::fromStdString(vm::numberText(nativeValue(a), nativeValue(b), domain_ == Domain::graphicState, desktop)); }
QVariant ViewDataDomain::parseNumber(const QVariant &a, const QString &b) const { const auto result = vm::parseNumber(nativeValue(a), b.toStdString(), true); return result.valid ? viewValue(result.value) : QVariant(); }
QVariant ViewDataDomain::parsed(const QVariant &a, const QString &b) const { const auto result = vm::parseNumber(nativeValue(a), b.toStdString(), false); return result.valid ? viewValue(result.value) : QVariant::fromValue(nullptr); }
QString ViewDataDomain::formatted(const QVariant &a, const QVariant &b, bool desktop) const {
    const auto value = nativeValue(b);
    if (value.kind() == ii::core::ValueKind::array && array(value).size() == 2) return viewValue(array(value)[0]).toString() + " — " + viewValue(array(value)[1]).toString() + viewValue(get(nativeValue(a), "unit")).toString();
    return numberText(a, b, desktop);
}
QVariant ViewDataDomain::buttonRows(double a, const QVariant &b, int c, double d, double e, bool f) const { return viewValue(vm::buttonRows(a, nativeValue(b), c, d, e, f)); }
void registerNativeViewModelTypes() {
    qmlRegisterType<GenerationProjection>("Dreamscapes.Storage", 1, 0, "NativeGenerationProjection");
    qmlRegisterType<VideoTimelineModel>("Dreamscapes.Storage", 1, 0, "NativeVideoTimeline");
    qmlRegisterUncreatableType<ViewDataDomain>("Dreamscapes.Storage", 1, 0, "NativeViewDataDomain", "Owned by NativeData");
    qmlRegisterSingletonType<ViewData>("Dreamscapes.Storage", 1, 0, "NativeData", [](QQmlEngine *, QJSEngine *) -> QObject * { return new ViewData; });
}
} // namespace dreamscapes
