#include "DreamscapesMcp.h"
#include "App/Generation/GenerationController.h"
#include <agent/ObjectTools.h>
#include <agent/McpServer.h>
#include <agent/QuestionInbox.h>
#include <agent/UserQuestions.h>
#include <mcp/LocalApplications.h>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <algorithm>

namespace a = iiLocalLLM::agent;
namespace m = iiLocalLLM::mcp;
namespace {
QJsonObject input(QJsonObject properties = {}, QJsonArray required = {}) {
    return {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
}
QJsonObject pageInput() {
    return input({{"offset", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 1000000}}},
                  {"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}});
}
a::ToolResult page(const QVariantList& list, const QJsonObject& args) {
    const auto offset = args["offset"].toInt(0), limit = args["limit"].toInt(20);
    return {"Dreamscapes items", {{"items", QJsonArray::fromVariantList(list.mid(offset, limit))},
        {"offset", offset}, {"total", list.size()}, {"has_more", offset + limit < list.size()}}};
}
QJsonObject status(GenerationController& controller) {
    return {{"connected", controller.connected()}, {"container_path", controller.containerPath()},
        {"selected_model", controller.selectedModel()}, {"model_count", controller.models().size()},
        {"job_count", controller.jobs().size()}, {"busy", controller.busy()},
        {"runtime_available", controller.runtimeAvailable()}, {"error", controller.errorString()},
        {"inference", QJsonObject::fromVariantMap(controller.inferenceStatus())},
        {"latest_image", controller.latestImage().toString()}};
}
}
void installDreamscapesMcp(QObject* root, QObject* lifetime) {
    if (qEnvironmentVariable("IILOCALLLM_DISABLE_APP_MCP") == "1") return;
    auto* controller = root->findChild<GenerationController*>("generationController");
    if (!controller) { qWarning() << "Dreamscapes MCP: generation controller is unavailable"; return; }
    try {
        auto registry = std::make_shared<a::ToolRegistry>();
        auto add = [&](QString name, QString description, QJsonObject schema, bool readOnly, auto handler) {
            a::ToolDefinition definition{name, description, schema, {}, readOnly, readOnly};
            registry->add(a::objectTool(controller, std::move(definition),
                [handler](QObject& object, const QJsonObject& args, const a::ToolContext& context) {
                    return handler(static_cast<GenerationController&>(object), args, context);
                }));
        };
        add("status", "Read Dreamscapes' local storage, selected model, queue and inference state.", input(), true,
            [](auto& object, const auto&, const auto&) { return a::ToolResult{"Dreamscapes state", status(object)}; });
        add("models", "List Dreamscapes models in the device's local Society container.", pageInput(), true,
            [](auto& object, const auto& args, const auto&) { return page(object.models(), args); });
        add("jobs", "List Dreamscapes' actual generation jobs and completion/error state.", pageInput(), true,
            [](auto& object, const auto& args, const auto&) { return page(object.jobs(), args); });
        add("select_model", "Select an existing model ID returned by Dreamscapes.models.",
            input({{"id", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}}}}, {"id"}), false,
            [](auto& object, const auto& args, const auto&) {
                const auto id = args["id"].toString(); const auto models = object.models();
                if (std::none_of(models.begin(), models.end(), [&](const auto& value) { return value.toMap().value("id").toString() == id; }))
                    return a::ToolResult{"The requested local model is not available", {}, true};
                object.setSelectedModel(id); return a::ToolResult{"Dreamscapes model selected", status(object)};
            });
        add("refresh_models", "Refresh Dreamscapes' local model catalog.", input(), false,
            [](auto& object, const auto&, const auto&) { object.refreshModels(); return a::ToolResult{"Dreamscapes models refreshed", status(object)}; });
        add("generate", "Submit image generation to this running Dreamscapes instance. Returns actual job IDs; poll jobs for completion.",
            input({{"prompt", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 32000}}},
                {"aspect_ratio", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"1:1", "4:3", "3:4", "16:9", "9:16"}}}},
                {"count", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}}}, {"prompt"}), false,
            [](auto& object, const auto& args, const auto&) {
                const auto count = args["count"].toInt(1);
                const auto id = object.enqueue(args["prompt"].toString(), args["aspect_ratio"].toString("1:1"), count);
                if (id.isEmpty()) return a::ToolResult{object.errorString(), {}, true};
                const auto jobs = object.jobs(); QJsonArray ids;
                // The UI returns newest jobs first; expose this submission in creation order.
                for (qsizetype index = count; index > 0; --index)
                    ids.append(jobs[index - 1].toMap().value("id").toString());
                return a::ToolResult{"Dreamscapes generation accepted", {{"first_job_id", id}, {"job_ids", ids}, {"accepted", true}}};
            });
        add("cancel", "Cancel a queued or running job owned by this Dreamscapes instance.",
            input({{"id", QJsonObject{{"type", "string"}, {"pattern", "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"}}}}, {"id"}), false,
            [](auto& object, const auto& args, const auto&) {
                const auto id = args["id"].toString(); const auto cancelled = object.cancel(id);
                return a::ToolResult{cancelled ? "Dreamscapes cancellation requested" : "No cancellable job with that ID",
                    {{"job_id", id}, {"cancelled", cancelled}}, !cancelled};
            });
        auto policy = std::make_shared<a::RulePolicy>(a::PermissionMode::Default,
            QList<a::PermissionRule>{{"select_model", a::PermissionBehavior::Allow}, {"refresh_models", a::PermissionBehavior::Allow},
                {"generate", a::PermissionBehavior::Allow}, {"cancel", a::PermissionBehavior::Allow}});
        a::McpServerOptions bridge; bridge.workingDirectory = QDir::currentPath(); bridge.appId = "com.iisacc.dreamscapes";
        auto* questions = new a::QuestionInbox(a::PermissionRequestsOptions{}, lifetime);
        registry->add(a::userQuestionTool({false, "markdown"}));
        bridge.tools.hooks.append(questions->hook());
        auto protocol = a::mcpServerOptions(registry, policy, std::move(bridge));
        protocol.experimentalCapabilities["iisacc/userQuestions"] = QJsonObject{
            {"schema", "iisacc.user-question/1"}, {"tool", "AskUserQuestion"},
            {"responseChannel", "local-ui"}, {"permissionRequests", false},
            {"previewFormat", "markdown"}, {"previewRendering", "plain-text"}};
        auto server = std::make_shared<m::LocalApplicationServer>(
            m::LocalApplicationIdentity{"com.iisacc.dreamscapes", "Dreamscapes", DREAMSCAPES_APP_VERSION},
            std::move(protocol));
        if (!server->listen()) { qWarning() << "Dreamscapes MCP:" << server->errorString(); return; }
        root->setProperty("agentQuestionInbox", QVariant::fromValue(questions));
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, lifetime, [questions, server] { questions->close(); server->close(); });
        QObject::connect(controller, &QObject::destroyed, lifetime, [questions, server] { questions->close(); server->close(); });
    } catch (const std::exception& error) { qWarning() << "Dreamscapes MCP:" << error.what(); }
}
