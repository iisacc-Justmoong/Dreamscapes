#include "CanvasPresets.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

static void initializeCatalogue() { Q_INIT_RESOURCE(canvas_presets); }
namespace {
double pixelsPerUnit(const QString &unit, double ppi) {
    if (unit == "px") return 1;
    if (unit == "mm") return ppi / 25.4;
    if (unit == "in") return ppi;
    return 0;
}
double roundedPixel(double value) {
    const double lower = std::floor(value);
    // The authored catalogue uses nearest-even rounding for exact half pixels.
    return value - lower == 0.5 && std::fmod(lower, 2.0) == 0 ? lower : std::floor(value + 0.5);
}
}

CanvasPresets::CanvasPresets(QObject *parent) : QObject(parent) {
    initializeCatalogue();
    QFile file(":/canvas/catalog.json");
    if (!file.open(QIODevice::ReadOnly)) qFatal("Canvas preset catalogue is unavailable");
    const auto document = QJsonDocument::fromJson(file.readAll());
    const auto categories = document.object().value("categories").toArray();
    if (categories.isEmpty()) qFatal("Canvas preset catalogue is invalid");
    for (int index = 0; index < categories.size(); ++index) {
        const auto category = categories[index].toObject();
        int count = 0;
        for (const auto &sectionValue : category.value("sections").toArray()) {
            const auto section = sectionValue.toObject();
            for (const auto &presetValue : section.value("presets").toArray()) {
                auto entry = presetValue.toObject().toVariantMap();
                entry.insert("category", category.value("name").toString());
                entry.insert("categoryIndex", index);
                entry.insert("section", section.value("name").toString());
                m_presets.append(entry);
                ++count;
            }
        }
        m_categories.append(QVariantMap{{"name", category.value("name").toString()},
            {"subtitle", category.value("subtitle").toString()}, {"count", count}});
    }
}

QVariantList CanvasPresets::sections(int category, const QString &query) const {
    QVariantList result;
    auto normalized = query.toLower().trimmed();
    normalized.replace(QRegularExpression("(?<=\\d)\\s*[×x]\\s*(?=\\d)"), " ");
    const auto words = normalized.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QString previousGroup;
    for (const auto &value : m_presets) {
        const auto entry = value.toMap();
        if (words.isEmpty() && entry.value("categoryIndex").toInt() != category) continue;
        const QString haystack = QString("%1 %2 %3 %4 %5 %6 %7 %8")
            .arg(entry.value("name").toString(), entry.value("category").toString(),
                 entry.value("section").toString(), entry.value("width").toString(),
                 entry.value("height").toString(), entry.value("unit").toString(),
                 entry.value("pixelWidth").toString(), entry.value("pixelHeight").toString()).toLower();
        bool matches = true;
        for (const auto &word : words) {
            if (word == "x" ? entry.value("section").toString() != "X" : !haystack.contains(word)) {
                matches = false;
                break;
            }
        }
        if (!matches) continue;
        const auto group = entry.value("category").toString() + " · " + entry.value("section").toString();
        if (group != previousGroup) {
            result.append(QVariantMap{{"name", words.isEmpty() ? entry.value("section") : QVariant(group)},
                                      {"presets", QVariantList{}}});
            previousGroup = group;
        }
        auto section = result.last().toMap();
        auto presets = section.value("presets").toList();
        presets.append(entry);
        section.insert("presets", presets);
        result.last() = section;
    }
    return result;
}

QVariantMap CanvasPresets::preset(const QString &id) const {
    for (const auto &value : m_presets) if (value.toMap().value("id").toString() == id) return value.toMap();
    return {};
}

QVariantMap CanvasPresets::specification(double width, double height, const QString &unit,
                                        double ppi, const QString &background) const {
    const auto invalid = [](const QString &error) { return QVariantMap{{"valid", false}, {"error", error}}; };
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
        return invalid(tr("Enter a positive width and height."));
    if (!std::isfinite(ppi) || ppi <= 0 || ppi > 2400)
        return invalid(tr("Resolution must be between 1 and 2400 PPI."));
    const double scale = pixelsPerUnit(unit, ppi);
    if (scale == 0) return invalid(tr("Choose px, mm or in."));
    if (unit == "px" && (std::floor(width) != width || std::floor(height) != height))
        return invalid(tr("Pixel dimensions must be whole numbers."));
    const double pixelWidth = roundedPixel(width * scale), pixelHeight = roundedPixel(height * scale);
    // Covers every catalogue format, including A0. Bound custom input before SDK allocation.
    if (pixelWidth < 1 || pixelHeight < 1 || pixelWidth > 32768 || pixelHeight > 32768
        || pixelWidth * pixelHeight > 268435456)
        return invalid(tr("Use dimensions up to 32768 px and 268 megapixels."));
    if (background != "White" && background != "Transparent" && background != "Black")
        return invalid(tr("Choose a canvas background."));
    return {{"valid", true}, {"error", ""}, {"width", width}, {"height", height},
        {"unit", unit}, {"ppi", ppi}, {"background", background},
        {"pixelWidth", static_cast<int>(pixelWidth)}, {"pixelHeight", static_cast<int>(pixelHeight)}};
}

double CanvasPresets::convert(double value, const QString &from, const QString &to, double ppi) const {
    const double source = pixelsPerUnit(from, ppi), target = pixelsPerUnit(to, ppi);
    if (!std::isfinite(value) || !std::isfinite(ppi) || ppi <= 0 || source <= 0 || target <= 0) return 0;
    return value * source / target;
}
