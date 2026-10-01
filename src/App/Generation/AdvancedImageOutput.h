#pragma once
#include <QJsonObject>
#include <QIODevice>
namespace dreamscapes {
bool writeAdvancedImage(const QString &source, QIODevice *destination, const QJsonObject &parameters,
    qint64 resolvedSeed, QString *error);
}
