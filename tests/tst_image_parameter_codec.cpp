#include "App/Generation/ImageParameterCodec.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>
#include <utility>

void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }

int main()
{
    try {
        for (const auto &[json, enabled] : {
                 std::pair{R"({})", true}, std::pair{R"({"hiresFix":null})", true},
                 std::pair{R"({"hiresFix":true})", true}, std::pair{R"({"hiresFix":false})", false}}) {
            const auto saved = QJsonDocument::fromJson(json).object();
            check(dreamscapes::imageHiresEnabled(saved) == enabled, "restored job used the wrong HiRes state");
            iiLocalDiffusion::ImageParameters restored;
            QString error;
            check(dreamscapes::imageParametersFromMap(saved.toVariantMap(), &restored, &error), "saved preset was rejected");
            check(std::get<bool>(restored.values.at("hiresFix")) == enabled, "saved preset used the wrong HiRes state");
            const auto replay = dreamscapes::imageParametersToMap(restored);
            check(replay.value("hiresFix").metaType().id() == QMetaType::Bool && replay.value("hiresFix").toBool() == enabled,
                  "preset round trip did not retain the typed HiRes state");
            const auto serialized = QJsonDocument(QJsonObject::fromVariantMap(replay)).toJson(QJsonDocument::Compact);
            const auto reopened = QJsonDocument::fromJson(serialized).object();
            check(reopened.value("hiresFix").isBool() && dreamscapes::imageHiresEnabled(reopened) == enabled,
                  "serialized preset did not preserve the boolean HiRes state");
        }
        for (const auto *json : {R"({"hiresFix":0})", R"({"hiresFix":"false"})"}) {
            const auto saved = QJsonDocument::fromJson(json).object();
            check(dreamscapes::imageHiresEnabled(saved), "non-boolean job value disabled HiRes");
            iiLocalDiffusion::ImageParameters restored;
            QString error;
            check(!dreamscapes::imageParametersFromMap(saved.toVariantMap(), &restored, &error), "invalid preset type was accepted");
        }
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
