#include "SocietyGenerationStorage.h"
#include <iiSocietyHelper.h>

namespace dreamscapes {
QString generationRuntimeDirectory(iiSocietyHelper::FileSystem &files, QString *error) {
    const auto path = files.ensureDirectory("models", ".society-runtime/iiLocalDiffusion");
    if (error) *error = files.errorString(); return path;
}
QString generationResourceDirectory(iiSocietyHelper::FileSystem &files, QString *error) {
    const auto path = files.ensureDirectory("models", ".generation-resources/iiLocalDiffusion");
    if (error) *error = files.errorString(); return path;
}
QString generationRuntimeDirectory(const QString &containerPath, QString *error) {
    iiSocietyHelper::FileSystem files(iiSocietyHelper::FileSystem::Initialization::Deferred);
    const auto path = files.open(containerPath) ? files.ensureDirectory("models", ".society-runtime/iiLocalDiffusion") : QString{};
    if (error) *error = files.errorString(); return path;
}
QString generationResourceDirectory(const QString &containerPath, QString *error) {
    iiSocietyHelper::FileSystem files(iiSocietyHelper::FileSystem::Initialization::Deferred);
    const auto path = files.open(containerPath) ? files.ensureDirectory("models", ".generation-resources/iiLocalDiffusion") : QString{};
    if (error) *error = files.errorString(); return path;
}
bool migrateLegacyQ8Cache(const QString &source, const QString &destination, const std::atomic_bool &cancelled, QString *error) {
    return iiSocietyHelper::migrateLegacyCache(source, destination, cancelled, error);
}
}
