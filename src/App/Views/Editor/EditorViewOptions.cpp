#include "EditorViewOptions.h"
#include "../../ViewModels/Native/ViewValue.h"
#include <QColor>
#include <QFile>
#include <QJsonDocument>
#include <algorithm>
#include <array>
#include <cmath>

using namespace dreamscapes::alignment;
namespace {
double n(const QVariantMap &v, const char *key) { return v.value(QLatin1String(key)).toDouble(); }
double pixels(double value, const QString &units, double extent, double ppi) {
    return units == "mm"   ? value * ppi / 25.4
           : units == "pt" ? value * ppi / 72
           : units == "%"  ? value * extent / 100
                           : value;
}
} // namespace
EditorViewOptions::EditorViewOptions(QObject *parent) : QObject(parent) {
    QFile f(":/editor/ViewOptions.json");
    if (f.open(QIODevice::ReadOnly))
        m_schema = QJsonDocument::fromJson(f.readAll()).toVariant().toMap();
    for (const auto &name : {"snap", "grid", "guides", "display"})
        if (auto *v = family(QLatin1String(name))) {
            const auto spec = m_schema.value(QLatin1String(name)).toMap();
            for (auto i = spec.cbegin(); i != spec.cend(); ++i)
                v->insert(i.key(), i.value().toMap().value("default"));
        }
}
QVariantMap *EditorViewOptions::family(const QString &name) {
    if (name == "snap")
        return &m_snap;
    if (name == "grid")
        return &m_grid;
    if (name == "guides")
        return &m_guides;
    if (name == "display")
        return &m_display;
    return nullptr;
}
bool EditorViewOptions::edit(const QString &section, const QString &key, const QVariant &input) {
    auto *values = family(section);
    const auto spec = m_schema.value(section).toMap().value(key).toMap();
    if (!values || spec.isEmpty())
        return false;
    QVariant value;
    const auto original = spec.value("default");
    if (original.metaType().id() == QMetaType::Bool) {
        if (input.metaType().id() != QMetaType::Bool)
            return false;
        value = input.toBool();
    } else if (original.metaType().id() == QMetaType::QString) {
        const auto text = input.toString();
        if (spec.contains("options") && !spec.value("options").toList().contains(text))
            return false;
        if (key == "color" && !QColor(text).isValid())
            return false;
        value = text;
    } else {
        bool ok = false;
        const auto number = input.toDouble(&ok);
        if (!ok || !std::isfinite(number) || number < spec.value("min", -1e9).toDouble() ||
            number > spec.value("max", 1e9).toDouble())
            return false;
        if (spec.value("integer").toBool() && number != std::round(number))
            return false;
        value = number;
    }
    if (values->value(key) == value)
        return true;
    if (section == "snap" && key == "preset")
        return preset(value.toString());
    if (section == "grid" && key == "type") {
        if (value == "Isometric" && m_grid.value("subdivisions").toInt() == 4)
            m_grid["subdivisions"] = 2;
        else if (value == "Rectangular" && m_grid.value("type") == "Isometric" &&
                 m_grid.value("subdivisions").toInt() == 2)
            m_grid["subdivisions"] = 4;
    }
    values->insert(key, value);
    if (section == "display" && key == "composite")
        m_display["outlines"] = value == "Outlines";
    else if (section == "display" && key == "outlines")
        m_display["composite"] = value.toBool() ? "Outlines" : "Composite";
    emit changed();
    return true;
}
void EditorViewOptions::reset(const QString &section) {
    if (auto *v = family(section)) {
        const auto spec = m_schema.value(section).toMap();
        for (auto i = spec.cbegin(); i != spec.cend(); ++i)
            v->insert(i.key(), i.value().toMap().value("default"));
        emit changed();
    }
}
bool EditorViewOptions::preset(const QString &name) {
    const QStringList names{"Smart", "Pixel", "Vector", "Layout"};
    const auto index = names.indexOf(name);
    if (index < 0)
        return false;
    static const std::array<QStringList, 4> targets{
        {{"canvas-edges", "canvas-centers", "layer-edges", "layer-centers", "guides", "anchors", "spacing",
          "size"},
         {"canvas-edges", "layer-edges", "guides", "pixel-edges"},
         {"canvas-edges", "canvas-centers", "layer-edges", "layer-centers", "guides", "anchors"},
         {"canvas-edges", "canvas-centers", "layer-edges", "layer-centers", "guides", "grid", "baselines",
          "spacing", "size"}}};
    for (const auto &key :
         {"canvas-edges", "canvas-centers", "layer-edges", "layer-centers", "guides", "grid", "pixel-edges",
          "pixel-centers", "anchors", "baselines", "spacing", "size"})
        m_snap[QLatin1String(key)] = targets[std::size_t(index)].contains(QLatin1String(key));
    m_snap["preset"] = name;
    m_snap["tolerance"] = std::array{8., 1., 6., 6.}[std::size_t(index)];
    m_snap["release"] = std::array{12., 2., 10., 12.}[std::size_t(index)];
    m_snap["angle-step"] = std::array{15., 90., 15., 90.}[std::size_t(index)];
    m_snap["scale-step"] = std::array{5., 100., 5., 10.}[std::size_t(index)];
    m_snap["angle"] = true;
    m_snap["scale"] = index >= 2;
    emit changed();
    return true;
}
Grid EditorViewOptions::gridGeometry(Rect reference, double ppi) const {
    Grid g;
    g.reference = reference;
    const auto units = m_grid.value("units").toString();
    const auto x = [&](const char *key) { return pixels(n(m_grid, key), units, reference.width, ppi); };
    const auto y = [&](const char *key) { return pixels(n(m_grid, key), units, reference.height, ppi); };
    g.type = GridType(
        QStringList{"Rectangular", "Isometric", "Polar", "Perspective", "Pixel", "Baseline", "Layout"}
            .indexOf(m_grid.value("type").toString()));
    g.origin = {reference.x + x("origin-x"), reference.y + y("origin-y")};
    if (g.type == GridType::Polar) {
        g.origin.x += reference.width / 2;
        g.origin.y += reference.height / 2;
    }
    g.spacingX = x("spacing-x");
    g.spacingY = y("spacing-y");
    g.rotation = n(m_grid, "rotation");
    g.subdivisions = int(n(m_grid, "subdivisions"));
    g.majorEvery = int(n(m_grid, "major-every"));
    g.axisAngle = n(m_grid, "axis-angle");
    if (g.type != GridType::Rectangular && g.type != GridType::Isometric)
        g.rotation = 0;
    if (g.type == GridType::Polar)
        g.subdivisions = 1;
    if (g.type == GridType::Isometric || g.type == GridType::Polar)
        g.spacingX = x("step");
    if (g.type == GridType::Pixel) {
        g.spacingX = g.spacingY = n(m_grid, "pixel-step");
        g.rotation = 0;
    }
    if (g.type == GridType::Baseline)
        g.spacingY = y("increment");
    g.baselineOffset = y("offset");
    g.spokes = int(n(m_grid, "spokes"));
    g.ringCount = int(n(m_grid, "ring-count"));
    g.startAngle = n(m_grid, "start-angle");
    g.sweep = n(m_grid, "sweep");
    g.columns = int(n(m_grid, "columns"));
    g.rows = int(n(m_grid, "rows"));
    g.gutterX = x("gutter-x");
    g.gutterY = y("gutter-y");
    g.marginX = x("margin-x");
    g.marginY = y("margin-y");
    g.projection = m_grid.value("projection") == "1-point" ? 1 : 2;
    g.density = int(n(m_grid, "density"));
    g.depthStep = n(m_grid, "depth-step");
    g.vanishing1 = {reference.x + reference.width * n(m_grid, "vp1-x") / 100,
                    reference.y + reference.height * (n(m_grid, "vp1-y") + n(m_grid, "horizon") - 50) / 100};
    g.vanishing2 = {reference.x + reference.width * n(m_grid, "vp2-x") / 100,
                    reference.y + reference.height * (n(m_grid, "vp2-y") + n(m_grid, "horizon") - 50) / 100};
    if (g.type == GridType::Layout || g.type == GridType::Perspective) {
        g.reference.x += x("origin-x");
        g.reference.y += y("origin-y");
    }
    g.vanishing1.x += x("origin-x");
    g.vanishing1.y += y("origin-y");
    g.vanishing2.x += x("origin-x");
    g.vanishing2.y += y("origin-y");
    g.adaptive = m_grid.value("adaptive").toBool();
    return g;
}
Guide EditorViewOptions::guideGeometry(Rect reference, double ppi) const {
    Guide g;
    g.type = GuideType(QStringList{"Single", "Layout", "Video safe", "Print / bleed", "Rule of thirds",
                                   "Golden ratio", "Diagonals"}
                           .indexOf(m_guides.value("type").toString()));
    const auto units = m_guides.value("units").toString();
    g.orientation =
        QStringList{"Horizontal", "Vertical", "Angle"}.indexOf(m_guides.value("orientation").toString());
    g.position =
        pixels(n(m_guides, "position"), units, g.orientation == 1 ? reference.width : reference.height, ppi);
    g.angle = n(m_guides, "angle");
    g.columns = int(n(m_guides, "columns"));
    g.rows = int(n(m_guides, "rows"));
    g.gutterX = pixels(n(m_guides, "gutter-x"), units, reference.width, ppi);
    g.gutterY = pixels(n(m_guides, "gutter-y"), units, reference.height, ppi);
    g.marginX = pixels(n(m_guides, "margin-x"), units, reference.width, ppi);
    g.marginY = pixels(n(m_guides, "margin-y"), units, reference.height, ppi);
    g.margins = m_guides.value("margins").toBool();
    g.actionSafe = m_guides.value("action-safe").toBool();
    g.titleSafe = m_guides.value("title-safe").toBool();
    g.actionInset = n(m_guides, "action-inset");
    g.titleInset = n(m_guides, "title-inset");
    g.bleed = n(m_guides, "bleed") * ppi / 25.4;
    g.safeMargin = n(m_guides, "safe-margin") * ppi / 25.4;
    g.cropInset = n(m_guides, "crop-inset") * ppi / 25.4;
    g.ratio = n(m_guides, "ratio");
    return g;
}
void EditorViewOptions::clearGuides() {
    if (m_placedGuides.empty())
        return;
    m_placedGuides.clear();
    emit guidesChanged();
}
void EditorViewOptions::addGuides(std::vector<PlacedGuide> guides) {
    if (m_guides.value("clear-existing").toBool())
        m_placedGuides.clear();
    const auto available = std::max(0, 4096 - int(m_placedGuides.size()));
    if (int(guides.size()) > available)
        guides.resize(std::size_t(available));
    m_placedGuides.insert(m_placedGuides.end(), guides.begin(), guides.end());
    m_guides["enabled"] = true;
    emit guidesChanged();
    emit changed();
}
bool EditorViewOptions::moveGuide(int index, Point delta) {
    if (m_guides.value("locked").toBool() || index < 0 || index >= int(m_placedGuides.size()) ||
        !std::isfinite(delta.x) || !std::isfinite(delta.y))
        return false;
    auto &l = m_placedGuides[std::size_t(index)].line;
    const auto dx = l.b.x - l.a.x, dy = l.b.y - l.a.y, length = std::hypot(dx, dy);
    if (length <= 0)
        return false;
    const auto nx = -dy / length, ny = dx / length, amount = delta.x * nx + delta.y * ny;
    l.a.x += nx * amount;
    l.a.y += ny * amount;
    l.b.x += nx * amount;
    l.b.y += ny * amount;
    emit guidesChanged();
    return true;
}
bool EditorViewOptions::removeGuide(int index) {
    if (m_guides.value("locked").toBool() || index < 0 || index >= int(m_placedGuides.size()))
        return false;
    m_placedGuides.erase(m_placedGuides.begin() + index);
    emit guidesChanged();
    return true;
}

bool EditorViewOptions::editText(const QString &section,const QString &key,const QString &input) {
    if(input.trimmed().isEmpty())return false;
    if(key=="color")return edit(section,key,input.trimmed());
    auto value=dreamscapes::vm::trim(input.toStdString());
    for(const auto *unit:{"px","mm","pt","%","°"})if(value.ends_with(unit)){value=dreamscapes::vm::trim(value.substr(0,value.size()-std::char_traits<char>::length(unit)));break;}
    const auto parsed=dreamscapes::vm::parseNumber(dreamscapes::vm::Value(dreamscapes::vm::Object{}),value,true);
    return parsed.valid&&edit(section,key,dreamscapes::vm::number(parsed.value));
}
