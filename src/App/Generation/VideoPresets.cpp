#include "GenerationController.h"
#include "VideoGeneration.h"
#include <iiFileProviderStorage.h>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <fstream>

namespace {
std::filesystem::path presetPath(const QString &preferences)
{
    return preferences.isEmpty() ? std::filesystem::path{}
        : std::filesystem::path((preferences + ".video-presets.json").toStdU16String());
}
QVariantMap readPresets(const std::filesystem::path &path, const QVariantMap &memory, QString &error)
{
    error.clear();
    if (path.empty()) return memory;
    std::error_code ec;
    const auto exists = std::filesystem::exists(path, ec);
    if (ec) { error = "Cannot read video presets."; return {}; }
    if (!exists) return {};
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 8 * 1024 * 1024) { error = "Invalid video preset file size."; return {}; }
    std::ifstream input(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(bytes), &parse);
    const auto root = document.object();
    if (!input || parse.error != QJsonParseError::NoError || !document.isObject()
        || root.value("schema") != "dreamscapes.video-presets/1" || !root.value("presets").isObject()
        || root.value("presets").toObject().size() > 100) {
        error = "Invalid video preset catalog."; return {};
    }
    return root.value("presets").toObject().toVariantMap();
}
QVariantMap controlsOnly(const QVariantMap &parameters)
{
    const QStringList fields{"prompt","negativePrompt","width","height","duration","fps","outputCount",
        "steps","cfgScale","seed","randomSeed","interpolationFactor","decodeTimestep","decodeNoiseScale",
        "imageConditionNoise","device","precision","offload","cpuTextEncoding","vaeTiling","crf",
        "encodingPreset","camera1","camera2","camera3","model"};
    QVariantMap saved;
    for (const auto &field : fields) if (parameters.contains(field)) saved.insert(field, parameters.value(field));
    return saved;
}
QString presetValidationError(const QVariantMap &saved)
{
    auto validated = QJsonObject::fromVariantMap(saved);
    if ((validated.contains("prompt") && !validated.value("prompt").isString())
        || (validated.contains("randomSeed") && !validated.value("randomSeed").isBool()))
        return QObject::tr("Invalid video preset values.");
    if (validated.contains("randomSeed") && !validated.value("randomSeed").toBool()
        && validated.value("seed").toDouble(-1) < 0)
        return QObject::tr("Enter a nonnegative fixed seed or enable random seed.");
    if (validated.value("prompt").toString().trimmed().isEmpty()) validated["prompt"] = "Preset validation";
    if (saved.value("randomSeed", true).toBool()) validated["seed"] = -1;
    QJsonArray camera;
    for (const auto &field : {"camera1","camera2","camera3"}) {
        const auto motion = saved.value(field, "none").toString();
        if (motion != "none") camera.append(motion);
    }
    validated["camera"] = camera.isEmpty() ? QJsonArray{"none"} : camera;
    QString error;
    if (!dreamscapes::normalizeVideoRecipe(&validated, &error)) return error;
    return {};
}

}

QStringList GenerationController::videoPresetNames() const
{
    QString error;
    return readPresets(presetPath(m_runtime.modelPreferencesFile), m_memoryVideoPresets, error).keys();
}

QVariantMap GenerationController::videoPreset(const QString &name)
{
    QString error;
    const auto presets = readPresets(presetPath(m_runtime.modelPreferencesFile), m_memoryVideoPresets, error);
    const auto value = controlsOnly(presets.value(name.trimmed()).toMap());
    if (error.isEmpty() && value.isEmpty()) error = tr("Video preset does not exist.");
    if (error.isEmpty()) error = presetValidationError(value);
    if (m_videoPresetError != error) { m_videoPresetError = error; emit videoPresetsChanged(); }
    return error.isEmpty() ? value : QVariantMap{};
}

bool GenerationController::saveVideoPreset(const QString &name, const QVariantMap &parameters)
{
    const auto fail = [this](const QString &error) {
        m_videoPresetError = error; emit videoPresetsChanged(); return false;
    };
    const auto key = name.trimmed();
    if (key.isEmpty() || key.size() > 100 || key.contains(QChar::Null) || key == tr("Default"))
        return fail(tr("Use a preset name of 1 to 100 characters other than Default."));
    // Presets contain controls only. Documents, shots, image keys and runtime
    // state belong to their own owners and are never captured by a preset.
    const auto saved = controlsOnly(parameters);
    QString error = presetValidationError(saved);
    if (!error.isEmpty()) return fail(error);
    const auto path = presetPath(m_runtime.modelPreferencesFile);
    auto presets = readPresets(path, m_memoryVideoPresets, error);
    if (!error.isEmpty()) return fail(error);
    if (presets.size() >= 100 && !presets.contains(key)) return fail(tr("At most 100 video presets are allowed."));
    presets.insert(key, saved);
    const auto bytes = QJsonDocument(QJsonObject{{"schema","dreamscapes.video-presets/1"},
        {"presets", QJsonObject::fromVariantMap(presets)}}).toJson();
    if (bytes.size() > 8 * 1024 * 1024) return fail(tr("Video presets exceed 8 MiB."));
    if (!path.empty()) {
        std::error_code ec;
        if (!path.parent_path().empty()) iiFileProvider::native::createDirectories(path.parent_path(), ec);
        if (ec) return fail(tr("Cannot create the video preset directory."));
        auto temporary = path; temporary += ".tmp";
        iiFileProvider::native::OutputFile output(temporary, std::ios::binary | std::ios::trunc);
        output.write(bytes.constData(), bytes.size()); output.close();
        if (output) iiFileProvider::native::renameFile(temporary, path, ec);
        if (!output || ec) {
            std::filesystem::remove(temporary, ec);
            return fail(tr("Cannot save video presets."));
        }
    } else m_memoryVideoPresets = presets;
    m_videoPresetError.clear(); emit videoPresetsChanged(); return true;
}
