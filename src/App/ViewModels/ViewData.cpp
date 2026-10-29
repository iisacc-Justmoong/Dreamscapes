#include "ViewData.h"
#include "GenerationProjection.h"
#include "SnapshotListModel.h"
#include "VideoTimelineModel.h"
#include "TimelinePlaybackModel.h"
#include <QDateTime>
#include <QFile>
#include <QJSValue>
#include <QQmlEngine>
#include <iiJson/Json.h>
#include <regex>

static void initializeNativeViewCatalogs() {
    Q_INIT_RESOURCE(native_view_catalogs);
}
namespace dreamscapes {
using namespace vm;
namespace {
std::optional<Value> loadNativeViewCatalog(std::string_view name) {
    static constexpr const char *names[] = {
        "tools", "text", "text-definition", "photo", "file", "compact", "elements", "graphic",
        "view-options", "field-groups", "background-defaults"};
    const auto known = std::ranges::find(names, name);
    if (known == std::end(names))
        return std::nullopt;
    static const bool initialized = [] { initializeNativeViewCatalogs(); return true; }();
    (void)initialized;
    QFile file(QStringLiteral(":/viewmodels/%1.json").arg(QLatin1StringView(*known)));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Missing native view catalog: %s", *known);
    const auto bytes = file.readAll();
    ii::json::Limits limits;
    limits.items = 500000; // The bundled 381-panel catalog is bounded and schema checked by tests.
    auto parsed = ii::json::parse(std::string_view(bytes.constData(), bytes.size()), limits);
    if (!parsed)
        qFatal("Invalid native view catalog %s: %s", *known, parsed.error().message.c_str());
    return std::move(*parsed);
}
} // namespace
Value nativeValue(const QVariant &input) {
    const auto value =
        input.metaType() == QMetaType::fromType<QJSValue>() ? input.value<QJSValue>().toVariant() : input;
    if (!value.isValid() || value.isNull())
        return {};
    switch (value.metaType().id()) {
    case QMetaType::QVariant:
        return nativeValue(value.value<QVariant>());
    case QMetaType::Bool:
        return Value(value.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::Long:
    case QMetaType::Short:
        return Value(value.toLongLong());
    case QMetaType::UInt:
    case QMetaType::ULongLong:
    case QMetaType::ULong:
    case QMetaType::UShort:
        return Value(value.toULongLong());
    case QMetaType::Float:
    case QMetaType::Double:
        return std::isfinite(value.toDouble()) ? Value(value.toDouble()) : Value();
    case QMetaType::QDateTime:
        return Value(value.toDateTime().toUTC().toString(Qt::ISODateWithMs).toStdString());
    case QMetaType::QDate:
        return Value(value.toDate().toString(Qt::ISODate).toStdString());
    case QMetaType::QTime:
        return Value(value.toTime().toString(Qt::ISODateWithMs).toStdString());
    case QMetaType::QVariantMap: {
        Object result;
        const auto map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            result[it.key().toStdString()] = nativeValue(it.value());
        return Value(std::move(result));
    }
    case QMetaType::QStringList:
    case QMetaType::QVariantList: {
        const auto items = value.toList();
        Array result;
        result.reserve(items.size());
        for (const auto &item : items)
            result.push_back(nativeValue(item));
        return Value(std::move(result));
    }
    default:
        return Value(value.toString().toStdString());
    }
}
QVariant viewValue(const Value &value) {
    return std::visit(
        [](const auto &v) -> QVariant {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<T, std::monostate>)
                return QVariant::fromValue(nullptr);
            else if constexpr (std::same_as<T, std::string>)
                return QString::fromStdString(v);
            else if constexpr (std::same_as<T, Object>) {
                QVariantMap result;
                for (const auto &[key, item] : v)
                    result[QString::fromStdString(key)] = viewValue(item);
                return result;
            } else if constexpr (std::same_as<T, Array>) {
                QVariantList result;
                result.reserve(v.size());
                for (const auto &item : v)
                    result.push_back(viewValue(item));
                return result;
            } else if constexpr (std::same_as<T, ii::core::Bytes>)
                return QByteArray(reinterpret_cast<const char *>(v.data()), v.size());
            else
                return QVariant::fromValue(v);
        },
        value.storage());
}
ViewData::ViewData(QObject *parent) : QObject(parent) {
    presentation_ = new ViewProjectionModel(this);
    catalog_ = std::make_unique<ViewCatalog>(ViewCatalog::Resources{}, loadNativeViewCatalog);
#define VIEW_DATA_CREATE(name) name##_ = new ViewDataDomain(*catalog_, Domain::name, this);
    VIEW_DATA_CREATE(tools)
    VIEW_DATA_CREATE(graphics)
    VIEW_DATA_CREATE(graphicState)
    VIEW_DATA_CREATE(file)
    VIEW_DATA_CREATE(elements)
    VIEW_DATA_CREATE(text) VIEW_DATA_CREATE(photo) VIEW_DATA_CREATE(compact) VIEW_DATA_CREATE(viewOptions)
        VIEW_DATA_CREATE(selection) VIEW_DATA_CREATE(storage) VIEW_DATA_CREATE(facts)
#undef VIEW_DATA_CREATE
}
ViewDataDomain::ViewDataDomain(ViewCatalog &catalog, Domain domain, QObject *parent)
    : QObject(parent), catalog_(catalog), domain_(domain) {}
QVariant ViewDataDomain::read(Read operation, std::initializer_list<QVariant> args) const {
    ViewRead request{operation, {}};
    for (const auto &arg : args)
        request.arguments.push_back(nativeValue(arg));
    const auto result = catalog_.queries(domain_).send(request);
    return result ? viewValue(*result) : QVariant();
}
QVariantList ViewDataDomain::shapes() const {
    return read(Read::shapes, {}).toList();
}
QVariantList ViewDataDomain::viewsData() const {
    return read(Read::views, {}).toList();
}
#define TEXT_DATA(name)                                                                                                \
    QVariantList ViewDataDomain::name() const {                                                                        \
        return viewValue(get(catalog_.resource("text"), #name)).toList();                                              \
    }
TEXT_DATA(roles)
TEXT_DATA(aliases)
TEXT_DATA(usages)
TEXT_DATA(typography)
TEXT_DATA(layout)
TEXT_DATA(appearance)
#undef TEXT_DATA
#define READ_ONE(name, op, type)                                                                                       \
    QVariant ViewDataDomain::name(const type &value) const {                                                           \
        return read(Read::op, {value});                                                                                \
    }
    READ_ONE(tool, tool, QString) READ_ONE(definition, definition, QString) READ_ONE(viewsFor, views, QString)
        READ_ONE(field, field, QString) READ_ONE(defaults, defaults, QVariant)
            READ_ONE(runtimeDefinition, runtimeDefinition, QVariant) READ_ONE(normalizedTool, normalizedTool, QString)
                READ_ONE(panelsFor, panelsFor, QString) READ_ONE(byId, byId, QString)
                    READ_ONE(overviewFor, overviewFor, QString) READ_ONE(defaultsFor, defaultsFor, QVariant)
                        READ_ONE(operationDefaults, operationDefaults, QString) READ_ONE(shapeView, shapeView, QString)
                            READ_ONE(roleValues, roleValues, QVariant) READ_ONE(readout, readout, QVariant)
                                READ_ONE(source, source, QVariant) READ_ONE(key, key, QVariant)
                                    READ_ONE(historyImages, historyImages, QVariant) READ_ONE(kind, kind, QVariant)
                                        READ_ONE(dimensions, dimensions, QVariant)
                                            READ_ONE(structure, structure, QVariant) READ_ONE(state, state, QVariant)
                                                READ_ONE(model, model, QVariant)
                                                    READ_ONE(resultSummary, resultSummary, QVariant)
#undef READ_ONE
                                                        QVariant ViewDataDomain::catalog() const {
    return read(Read::catalog, {});
}
QVariant ViewDataDomain::view(const QString &a, const QString &b) const {
    return read(Read::view, {a, b});
}
QVariant ViewDataDomain::applicableFields(const QVariant &a, const QVariant &b) const {
    return read(Read::applicableFields, {a, b});
}
QVariant ViewDataDomain::rows(const QVariant &a, bool b) const {
    return read(Read::rows, {a, b});
}
QVariant ViewDataDomain::controls(const QString &a, const QString &b) const {
    return read(Read::controls, {a, b});
}
QVariant ViewDataDomain::fillColor(const QString &a, const QString &b) const {
    return read(Read::fillColor, {a, b});
}
QVariant ViewDataDomain::sections(const QString &a, const QVariant &b) const {
    return read(Read::sections, {a, b});
}
bool ViewDataDomain::isImage(const QVariant &a) const {
    return read(Read::isImage, {a}).toBool();
}
QVariant ViewDataDomain::date(const QVariant &a) const {
    const auto map = viewValue(nativeValue(a)).toMap();
    if (!map.value("dateText").toString().isEmpty())
        return map.value("dateText");
    const auto d = QDateTime::fromString(map.value("modified").toString(), Qt::ISODateWithMs);
    return d.isValid() ? d.toLocalTime().toString("yyyy-MM-dd") : QStringLiteral("Unavailable");
}
QVariant ViewDataDomain::value(const QVariant &a, const QString &key) const {
    const auto data = nativeValue(a);
    const auto name = key.toStdString();
    if (has(data, name) && get(data, name).kind() != ii::core::ValueKind::null)
        return viewValue(get(data, name));
    for (auto parent : {"generationParameters", "parameters", "metadata"})
        if (has(data, parent))
            return has(get(data, parent), name) ? viewValue(get(get(data, parent), name)) : QVariant();
    return {};
}
QVariant ViewDataDomain::sectionFiles(const QVariant &a, const QString &b) const {
    return read(Read::sectionFiles, {a, b});
}
QVariant ViewDataDomain::listing(const QVariant &a, const QString &b, const QString &c, const QString &d,
                                 const QString &e) const {
    return read(Read::listing, {a, b, c, d, e});
}
QVariant ViewDataDomain::clone(const QVariant &a) const {
    return a.isValid() ? viewValue(nativeValue(a)) : QVariant();
}
QStringList ViewDataDomain::pathParts(const QString &path) const {
    QStringList result;
    for (const auto &p : vm::pathParts(path.toStdString()))
        result.push_back(QString::fromStdString(p));
    return result;
}
QVariant ViewDataDomain::getPath(const QVariant &a, const QString &b) const {
    const auto result = pathValue(nativeValue(a), b.toStdString());
    return result ? viewValue(*result) : QVariant();
}
QVariant ViewDataDomain::withPath(const QVariant &a, const QString &b, const QVariant &c) const {
    return viewValue(vm::withPath(nativeValue(a), b.toStdString(), nativeValue(c)));
}
QVariant ViewDataDomain::nested(const QVariant &a, const QVariant &b) const {
    return viewValue(vm::nested(nativeValue(a), nativeValue(b)));
}
QVariant ViewDataDomain::flatten(const QVariant &a, const QString &b) const {
    return viewValue(vm::flatten(nativeValue(a), b.toStdString()));
}
QString ViewDataDomain::resolvedPath(const QString &a, const QVariant &b) const {
    return QString::fromStdString(vm::resolvedPath(a.toStdString(), nativeValue(b)));
}
QString ViewDataDomain::numberText(const QVariant &a, const QVariant &b, bool desktop) const {
    return QString::fromStdString(
        vm::numberText(nativeValue(a), nativeValue(b), domain_ == Domain::graphicState, desktop));
}
QVariant ViewDataDomain::parseNumber(const QVariant &a, const QString &b) const {
    const auto result = vm::parseNumber(nativeValue(a), b.toStdString(), true);
    return result.valid ? viewValue(result.value) : QVariant();
}
QVariant ViewDataDomain::parsed(const QVariant &a, const QString &b) const {
    const auto result = vm::parseNumber(nativeValue(a), b.toStdString(), false);
    return result.valid ? viewValue(result.value) : QVariant::fromValue(nullptr);
}
QString ViewDataDomain::formatted(const QVariant &a, const QVariant &b, bool desktop) const {
    const auto value = nativeValue(b);
    if (value.kind() == ii::core::ValueKind::array && array(value).size() == 2)
        return viewValue(array(value)[0]).toString() + " — " + viewValue(array(value)[1]).toString() +
               viewValue(get(nativeValue(a), "unit")).toString();
    return numberText(a, b, desktop);
}
QVariant ViewDataDomain::numberInput(const QString &input, double minimum, double maximum, const QString &unit) const {
    const auto field = vm::Value(
        vm::Object{{"min", vm::Value(minimum)}, {"max", vm::Value(maximum)}, {"unit", vm::Value(unit.toStdString())}});
    const auto result = vm::parseNumber(field, input.toStdString(), true);
    return result.valid ? viewValue(result.value) : QVariant::fromValue(nullptr);
}
QVariant ViewDataDomain::dimensionInput(const QVariant &value, int index, const QString &input) const {
    const auto field = vm::Value(vm::Object{
        {"min", vm::Value(1)}, {"max", vm::Value(65536)}, {"unit", vm::Value("px")}, {"integer", vm::Value(true)}});
    const auto number = vm::parseNumber(field, input.toStdString(), true);
    auto values = vm::array(nativeValue(value));
    if (!number.valid || index < 0 || index >= values.size())
        return QVariant::fromValue(nullptr);
    values[index] = number.value;
    return viewValue(vm::Value(std::move(values)));
}
QVariant ViewDataDomain::hexInput(const QString &input) const {
    const auto value = input.trimmed();
    static const std::regex hex("^#[0-9a-fA-F]{6}$");
    return std::regex_match(value.toStdString(), hex) ? QVariant(value.toUpper()) : QVariant::fromValue(nullptr);
}
QVariant ViewDataDomain::buttonRows(double a, const QVariant &b, int c, double d, double e, bool f) const {
    return viewValue(vm::buttonRows(a, nativeValue(b), c, d, e, f));
}
QVariantList ViewDataDomain::filesOfKind(const QVariantList &files, const QString &kind) const {
    QVariantList result;
    for (const auto &index : read(Read::fileIndices, {files, kind}).toList())
        result.push_back(files.at(index.toInt()));
    return result;
}
int ViewDataDomain::fileIndex(const QVariantList &files, const QString &key) const {
    return read(Read::fileIndex, {files, key}).toInt();
}
QVariantMap ViewDataDomain::fileForKey(const QVariantList &files, const QString &key, const QString &field) const {
    const auto index = read(Read::fileIndex, {files, key, field}).toInt();
    return index >= 0 ? files.at(index).toMap() : QVariantMap{};
}
QString ViewDataDomain::reconciledFileKey(const QVariantList &files, const QString &wanted) const {
    if (fileIndex(files, wanted) >= 0)
        return wanted;
    return files.isEmpty() ? QString{} : key(files.front()).toString();
}
QVariantMap ViewDataDomain::merged(const QVariantMap &base, const QVariantMap &patch) const {
    const auto plan = read(Read::mergedFields, {base, patch}).toMap();
    QVariantMap result;
    for (auto it = plan.begin(); it != plan.end(); ++it)
        result.insert(it.key(), patch.contains(it.key()) ? patch.value(it.key()) : base.value(it.key()));
    return result;
}
int ViewDataDomain::stateCount(const QVariantList &rows, const QStringList &states) const {
    return read(Read::stateIndices, {rows, states}).toList().size();
}
QVariantMap ViewDataDomain::firstState(const QVariantList &rows, const QString &state) const {
    const auto indices = read(Read::stateIndices, {rows, QStringList{state}}).toList();
    return indices.isEmpty() ? QVariantMap{} : rows.at(indices.front().toInt()).toMap();
}
int ViewDataDomain::sectionRowCount(const QVariant &view) const {
    return read(Read::sectionRowCount, {view}).toInt();
}
bool ViewDataDomain::containsView(const QString &id) const {
    return read(Read::containsView, {id}).toBool();
}
QVariantList ViewDataDomain::fieldsByType(const QVariant &fields, const QString &type) const {
    return read(Read::fieldsByType, {fields, type}).toList();
}
void registerNativeViewModelTypes() {
    qmlRegisterType<TimelinePlaybackModel>("Dreamscapes.Storage", 1, 0, "NativeTimelinePlayback");
    qmlRegisterUncreatableType<ViewProjectionModel>("Dreamscapes.Storage", 1, 0, "NativeViewProjection",
                                                   "Owned by NativeData");
    qmlRegisterType<SnapshotListModel>("Dreamscapes.Storage", 1, 0, "NativeSnapshotListModel");
    qmlRegisterType<GenerationProjection>("Dreamscapes.Storage", 1, 0, "NativeGenerationProjection");
    qmlRegisterType<VideoTimelineModel>("Dreamscapes.Storage", 1, 0, "NativeVideoTimeline");
    qmlRegisterUncreatableType<ViewDataDomain>("Dreamscapes.Storage", 1, 0, "NativeViewDataDomain",
                                               "Owned by NativeData");
    qmlRegisterSingletonType<ViewData>("Dreamscapes.Storage", 1, 0, "NativeData",
                                       [](QQmlEngine *, QJSEngine *) -> QObject * { return new ViewData; });
}
} // namespace dreamscapes
