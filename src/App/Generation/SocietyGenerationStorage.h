#pragma once
#include <SharedStorage.h>
#include <atomic>
#include <iiSocietyHelper.h>

namespace dreamscapes {
// Both paths are managed by Society. Runtime derivatives are device-local;
// the resource library is replicated by Society with the source models.
QString generationRuntimeDirectory(const QString &containerPath, QString *error);
QString generationRuntimeDirectory(iiSocietyHelper::FileSystem &files, QString *error);
inline QString generationRuntimeDirectory(const iiSocietyContainer::SharedStorage &storage, QString *error) { return generationRuntimeDirectory(storage.drive().rootPath(), error); }
QString generationResourceDirectory(const QString &containerPath, QString *error);
QString generationResourceDirectory(iiSocietyHelper::FileSystem &files, QString *error);
inline QString generationResourceDirectory(const iiSocietyContainer::SharedStorage &storage, QString *error) { return generationResourceDirectory(storage.drive().rootPath(), error); }
bool migrateLegacyQ8Cache(const QString &source, const QString &destination,
    const std::atomic_bool &cancelled, QString *error);
}
