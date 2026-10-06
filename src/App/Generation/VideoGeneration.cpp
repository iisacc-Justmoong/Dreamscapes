#include "VideoGeneration.h"
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QObject>
#include <cmath>
#include <limits>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace dreamscapes {
QSize videoSize(const QString &ratio)
{
    // Exact ratios on LTX's 32px grid, independent of image-generation resolution.
    if (ratio == "1:1") return {512,512};
    if (ratio == "4:3") return {640,480};
    if (ratio == "3:4") return {480,640};
    if (ratio == "16:9") return {1024,576};
    if (ratio == "9:16") return {576,1024};
    return {};
}
bool isVideoModel(const QString &directory)
{
    QFile file(directory + "/model_index.json");
    if (file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly)) return false;
    const auto type = QJsonDocument::fromJson(file.readAll()).object().value("_class_name").toString();
    return type == "LTXPipeline" || type == "LTXConditionPipeline" || type == "LTXImageToVideoPipeline";
}
bool validateVideoOutput(const QString &path, const QJsonObject &generation,
    const QSize &size, int frames, int fps, QString *error)
{
    const auto reject = [error] { *error = "The video engine returned an incomplete or inconsistent MP4 result."; return false; };
    const auto video = generation.value("video").toObject();
    const auto dimensions = video.value("size").toArray();
    const QFileInfo info(path);
    if (generation.value("schema") != "iild-temporal-video-v1" || generation.value("status") != "complete"
        || !video.value("verified_decode").toBool() || video.value("codec") != "h264"
        || video.value("frame_count").toInt() != frames || video.value("fps").toDouble() != fps
        || dimensions.size() != 2 || dimensions[0].toInt() != size.width() || dimensions[1].toInt() != size.height()
        || std::abs(video.value("duration_seconds").toDouble() - double(frames)/fps) > 1.0/fps
        || !info.isFile() || info.isSymLink() || info.canonicalFilePath() != path || info.size() < 12
        || video.value("size_bytes").toInteger() != info.size()) return reject();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.peek(12).mid(4,4) != "ftyp") return reject();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || QString::fromLatin1(hash.result().toHex()) != video.value("sha256").toString()) return reject();
    return true;
}
}

namespace dreamscapes {
bool normalizeVideoRecipe(QJsonObject *recipe, QString *error)
{
    auto &v = *recipe;
    auto reject = [&](const QString &message) { if (error) *error = message; return false; };
    auto number = [&](const char *key, double fallback, double low, double high, bool integer = false) {
        const auto raw = v.contains(key) ? v.value(key) : QJsonValue(fallback);
        const auto value = raw.toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!raw.isDouble() || !std::isfinite(value) || value < low || value > high || (integer && std::floor(value) != value)) return false;
        v[key] = value; return true;
    };
    if (!number("width",1024,32,4096,true) || !number("height",576,32,4096,true)
        || v.value("width").toInt() % 32 || v.value("height").toInt() % 32)
        return reject(QObject::tr("Video dimensions must be multiples of 32 between 32 and 4096."));
    if (!number("duration",5,1,30,true) || !number("fps",24,12,30,true)
        || !QList<int>{12,24,30}.contains(v.value("fps").toInt())
        || !number("outputCount",1,1,1000,true) || !number("seed",-1,-1,4294967295.,true)
        || !number("steps",30,1,1000,true) || !number("cfgScale",3,0,100)
        || !number("interpolationFactor",2,2,8,true) || !number("decodeTimestep",.05,0,1)
        || !number("decodeNoiseScale",.025,0,1) || !number("imageConditionNoise",0,0,1)
        || !number("crf",18,0,51,true)) return reject(QObject::tr("Correct invalid video numeric parameters."));
    for (const auto &key : {"cpuTextEncoding", "vaeTiling"}) {
        if (v.contains(key) && !v.value(key).isBool()) return reject(QObject::tr("Invalid video execution toggle."));
        if (!v.contains(key)) v[key] = true;
    }
    const QMap<QString,QStringList> options{
        {"device",{"auto","cpu","mps","cuda","rocm"}}, {"precision",{"auto","float32","float16","bfloat16"}},
        {"offload",{"auto","none","model","sequential"}},
        {"encodingPreset",{"medium","ultrafast","superfast","veryfast","faster","fast","slow","slower","veryslow"}}};
    for (auto i = options.cbegin(); i != options.cend(); ++i) {
        if (!v.contains(i.key())) v[i.key()] = i.value().first();
        if (!i.value().contains(v.value(i.key()).toString())) return reject(QObject::tr("Invalid video option: %1").arg(i.key()));
    }
    if (v.value("device") == "cpu" && QStringList{"model","sequential"}.contains(v.value("offload").toString()))
        return reject(QObject::tr("GPU offload requires a GPU execution device."));
    if (!v.value("prompt").isString() || v.value("prompt").toString().trimmed().isEmpty()
        || v.value("prompt").toString().size() > 32000 || v.value("negativePrompt").toString().size() > 32000)
        return reject(QObject::tr("Enter a video prompt of at most 32000 characters."));
    if (v.contains("shots") && !v.value("shots").isArray()) return reject(QObject::tr("Video shots must be an array."));
    if (v.contains("negativePrompt") && !v.value("negativePrompt").isString()) return reject(QObject::tr("Negative prompt must be text."));
    auto shots = v.value("shots").toArray();
    if (!v.contains("shots")) shots.append(QJsonObject{{"frames",v.value("duration").toInt()*v.value("fps").toInt()}});
    if (shots.isEmpty() || shots.size() > 256) return reject(QObject::tr("Keep at least one shot and at most 256 shots."));
    QJsonArray normalized;
    int total = 0;
    for (const auto &entry : shots) {
        if (!entry.isObject()) return reject(QObject::tr("Invalid video shot."));
        const auto shot = entry.toObject();
        const auto frames = shot.value("frames").toDouble();
        if (!std::isfinite(frames) || frames != std::floor(frames) || frames < 2 || frames > 4097)
            return reject(QObject::tr("Every video shot needs 2 to 4097 output frames."));
        auto prompt = shot.value("prompt").toString();
        if (prompt.trimmed().isEmpty()) prompt = v.value("prompt").toString();
        if (prompt.size() > 32000) return reject(QObject::tr("A shot prompt exceeds 32000 characters."));
        if (shot.contains("conditions") && !shot.value("conditions").isArray()) return reject(QObject::tr("Image conditions must be an array."));
        const auto conditions = shot.value("conditions").toArray();
        if (conditions.size() > 32) return reject(QObject::tr("Use at most 32 image keyframes per shot."));
        QSet<int> positions;
        for (const auto &value : conditions) {
            const auto condition = value.toObject();
            const auto frame = condition.value("frame").toDouble(-1), strength = condition.value("strength").toDouble(1);
            if (!value.isObject() || frame != std::floor(frame) || frame < 0 || frame >= frames
                || !std::isfinite(strength) || strength <= 0 || strength > 1 || positions.contains(int(frame))
                || condition.value("image").toString().isEmpty()) return reject(QObject::tr("Correct image keyframe positions and weights."));
            positions.insert(int(frame));
        }
        normalized.append(QJsonObject{{"frames",int(frames)},{"prompt",prompt},
            {"negative_prompt",v.value("negativePrompt").toString()},{"conditions",conditions}});
        total += int(frames);
    }
    if (total > 4097) return reject(QObject::tr("A composition supports at most 4097 output frames."));
    v["shots"] = normalized; v["frames"] = total;
    return true;
}
}
