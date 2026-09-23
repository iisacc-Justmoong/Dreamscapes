#include "SocietyGenerationStorage.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace dreamscapes {
QString generationRuntimeDirectory(const iiSocietyContainer::SharedStorage &storage, QString *error)
{
    return storage.ensureDirectory(iiSocietyContainer::StoreSection::Models,
        ".society-runtime/iiLocalDiffusion", error);
}
QString generationResourceDirectory(const iiSocietyContainer::SharedStorage &storage, QString *error)
{
    return storage.ensureDirectory(iiSocietyContainer::StoreSection::Models,
        ".generation-resources/iiLocalDiffusion", error);
}
namespace {
bool regularTree(const QString &path)
{
    const QFileInfo info(path);
    if (info.isSymLink() || info.isJunction() || info.canonicalFilePath() != path) return false;
    if (info.isFile()) return true;
    if (!info.isDir()) return false;
    for (const auto &entry : QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
        if (!regularTree(entry.absoluteFilePath())) return false;
    return true;
}
QByteArray digest(const QString &path, const std::atomic_bool &cancelled)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly)) return {};
    while (!file.atEnd()) {
        if (cancelled) return {};
        const auto bytes = file.read(1024 * 1024);
        if (file.error() != QFileDevice::NoError) return {};
        hash.addData(bytes);
    }
    return hash.result();
}
}
bool migrateLegacyQ8Cache(const QString &source, const QString &destination,
    const std::atomic_bool &cancelled, QString *error)
{
    if (source.isEmpty() || (!QFileInfo::exists(source) && !QFileInfo(source).isSymLink())) return true;
    if (!regularTree(source) || !regularTree(destination) || source == destination
        || destination.startsWith(source + '/') || source.startsWith(destination + '/')) {
        if (error) *error = "The previous model cache was redirected and has been preserved.";
        return false;
    }
    const auto move = [&](auto &&self, const QString &from, const QString &to) -> bool {
        for (const auto &entry : QDir(from).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
            if (cancelled) return false;
            const auto target = QDir(to).filePath(entry.fileName());
            if (entry.isDir()) {
                if (!QFileInfo::exists(target) && !QDir().mkdir(target)) return false;
                if (!regularTree(target) || !QFileInfo(target).isDir() || !self(self, entry.filePath(), target)) return false;
            } else if (QFileInfo::exists(target)) {
                const auto original = digest(entry.filePath(), cancelled);
                if (original.isEmpty() || !regularTree(target) || original != digest(target, cancelled)
                    || cancelled
                    || !QFile::remove(entry.filePath())) return false;
            } else if (!QFile::rename(entry.filePath(), target)) return false;
        }
        return QDir().rmdir(from);
    };
    if (!move(move, source, destination)) {
        if (error) *error = cancelled ? "Model cache migration was cancelled."
            : "Cannot move the previous model cache to Society. Existing files were preserved.";
        return false;
    }
    return true;
}
}
