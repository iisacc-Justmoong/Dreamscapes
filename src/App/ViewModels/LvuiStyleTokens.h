#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QString>

namespace dreamscapes {

// Compatibility boundary for the existing QML views. Values and names belong
// to LVUI; this object neither defines nor overrides product style tokens.
class LvuiStyleTokens final : public QObject {
    Q_OBJECT
public:
    explicit LvuiStyleTokens(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE QColor color(const QString &name);
    Q_INVOKABLE qreal metric(const QString &name);

private:
    QHash<QString, QColor> colors_;
    QHash<QString, qreal> metrics_;
};

void registerLvuiStyleTypes();
} // namespace dreamscapes
