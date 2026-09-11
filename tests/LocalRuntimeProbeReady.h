#pragma once
#include <QJsonValue>

// Qt can serialize the Image.Status enum as its key rather than a number.
inline bool dreamscapesProbeImageReady(const QJsonValue &status)
{
    return status.toInt(-1) == 1 || status.toString() == QStringLiteral("Ready");
}
