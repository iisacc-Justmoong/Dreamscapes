#include "LvuiStyleTokens.h"

#include <LVUI/StyleTokens.h>
#include <QDebug>
#include <QtQml/qqml.h>
#include <limits>

namespace dreamscapes {

QColor LvuiStyleTokens::color(const QString &name) {
    if (const auto found = colors_.constFind(name); found != colors_.cend())
        return *found;
    const auto resolved = lvui::default_style_tokens().color(name.toStdString());
    if (!resolved) {
        qWarning().noquote() << "LVUI color token" << name << ":"
                             << QString::fromStdString(resolved.error().message);
        return {};
    }
    const auto color = QColor::fromRgbF(resolved->red, resolved->green, resolved->blue, resolved->alpha);
    colors_.insert(name, color);
    return color;
}

qreal LvuiStyleTokens::metric(const QString &name) {
    if (const auto found = metrics_.constFind(name); found != metrics_.cend())
        return *found;
    const auto resolved = lvui::default_style_tokens().number(name.toStdString());
    if (!resolved) {
        qWarning().noquote() << "LVUI metric token" << name << ":"
                             << QString::fromStdString(resolved.error().message);
        return std::numeric_limits<qreal>::quiet_NaN();
    }
    metrics_.insert(name, *resolved);
    return *resolved;
}

void registerLvuiStyleTypes() {
    static const bool registered = [] {
        qmlRegisterSingletonType<LvuiStyleTokens>("Dreamscapes.Style", 1, 0, "Tokens",
                                                 [](QQmlEngine *, QJSEngine *) -> QObject * {
                                                     return new LvuiStyleTokens;
                                                 });
        return true;
    }();
    (void)registered;
}
} // namespace dreamscapes
