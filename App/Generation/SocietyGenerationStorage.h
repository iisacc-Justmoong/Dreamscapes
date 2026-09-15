#pragma once
#include <SharedStorage.h>
#include <atomic>

namespace dreamscapes {
// Both paths are managed by Society. Runtime derivatives are device-local;
// the resource library is replicated by Society with the source models.
QString generationRuntimeDirectory(const iiSocietyContainer::SharedStorage &storage, QString *error);
QString generationResourceDirectory(const iiSocietyContainer::SharedStorage &storage, QString *error);
bool migrateLegacyQ8Cache(const QString &source, const QString &destination,
    const std::atomic_bool &cancelled, QString *error);
}
