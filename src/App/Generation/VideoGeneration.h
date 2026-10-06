#pragma once
#include <QJsonObject>
#include <QSize>
#include <QString>

namespace dreamscapes {
bool normalizeVideoRecipe(QJsonObject *recipe, QString *error);
QSize videoSize(const QString &ratio);
bool isVideoModel(const QString &directory);
// Validate the SDK's decoded-output report against the bytes being published.
bool validateVideoOutput(const QString &path, const QJsonObject &generation,
    const QSize &size, int frames, int fps, QString *error);
}
