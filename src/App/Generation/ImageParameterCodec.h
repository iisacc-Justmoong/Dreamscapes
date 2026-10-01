#pragma once
#include <Generation/ImageParameters.hpp>
#include <QVariantMap>
#include <QVariantList>

namespace dreamscapes {
QVariantMap imageParametersToMap(const iiLocalDiffusion::ImageParameters &parameters);
bool imageParametersFromMap(const QVariantMap &map, iiLocalDiffusion::ImageParameters *parameters, QString *error);
QVariantList imageParameterIssues(const std::vector<iiLocalDiffusion::ImageParameterIssue> &issues);
QVariantList imageParameterSchema();
}
