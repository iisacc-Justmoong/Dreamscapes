#pragma once
#include <iiMultimodalAgent/diagnostics/FailureReport.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace dreamscapes {
inline QJsonObject failureEvidenceReport(const QJsonObject& evidence) {
    using namespace iiMultimodalAgent::diagnostics;
    FailureContext context;
    const auto job = evidence["job"].toObject(), status = evidence["inference"].toObject();
    context.jobId = job["id"].toString().toStdString();
    context.operation = job["mediaType"].toString("Generation").toStdString();
    auto phase = status["phase"].toString();
    if (phase.isEmpty()) phase = status["stage"].toString();
    if (phase.isEmpty()) phase = status["state"].toString("not-reported");
    context.stage = phase.toStdString();
    context.backend = status["backend"].toString().toStdString();
    context.error = evidence["rawError"].toString().toStdString();
    context.workerLog = evidence["workerLog"].toString().toStdString();
    context.tracePath = evidence["tracePath"].toString().toStdString();
    context.traceTail = evidence["traceTail"].toString().toStdString();
    context.timedOut = evidence["timedOut"].toBool();
    if (status.contains("step")) context.step = status["step"].toInteger();
    if (status.contains("total")) context.total = status["total"].toInteger();
    if (evidence.contains("exitCode")) context.exitCode = evidence["exitCode"].toInteger();
    for (const auto& key : {"model", "modelName", "vae", "width", "height", "seed", "steps", "device", "prompt",
             "negativePrompt", "aspectRatio", "video", "parameters", "advanced", "advancedParameters", "videoParameters", "generation", "execution", "workerExitStatus", "startedAt", "finishedAt"}) {
        const auto value = job[key];
        if (value.isUndefined()) continue;
        const auto rendered = value.isObject() ? QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact))
            : value.isArray() ? QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)) : value.toVariant().toString();
        context.parameters[key] = rendered.toStdString();
    }
    const auto telemetry = job["telemetry"].toObject();
    if (evidence.contains("traceReadError")) context.parameters["traceReadError"] = evidence["traceReadError"].toString().toStdString();
    if (!telemetry.isEmpty()) context.parameters["lastTelemetry"] = QJsonDocument(telemetry).toJson(QJsonDocument::Compact).toStdString();
    const auto report = explain(context);
    QJsonArray checks; for (const auto& check : report.checks) checks.append(QString::fromStdString(check));
    return {{"schema", "iisacc.failure-analysis/1"}, {"jobId", job["id"]}, {"state", "pending"},
        {"engine", "iiMultimodalAgent"}, {"reasoner", "native-evidence"}, {"category", QString::fromStdString(report.category)},
        {"summary", QString::fromStdString(report.summary)}, {"details", QString::fromStdString(report.details)},
        {"checks", checks}, {"rawError", evidence["rawError"]}, {"evidence", evidence}, {"reportPath", evidence["reportPath"]}};
}
}
