#include "HomeReferenceStore.h"
#include "../ViewModels/Native/GenerationDraft.h"
#include <QCoreApplication>
#include <QImageReader>
#include <QStringList>
#include <iiSocietyHelper>

namespace dreamscapes {
namespace {
QString message(const char *text) { return QCoreApplication::translate("HomeCanvas", text); }
}
QSize homeReferenceOutputSize(const QString &ratio) {
    if (!QStringList{"1:1","4:3","3:4","16:9","9:16"}.contains(ratio)) return {};
    const auto size=vm::imageSize(ratio.toStdString(),vm::defaultImageShortSide);
    return {int(vm::number(vm::get(size,"width"))),int(vm::number(vm::get(size,"height")))};
}
bool HomeReferenceStore::fail(QString value) { error_ = std::move(value); return error_.isEmpty(); }
QString HomeReferenceStore::key(const QVariantMap &entry) { return entry.value("source").toUrl().toString(QUrl::FullyEncoded); }
bool HomeReferenceStore::add(const QUrl &source) {
    if (!source.isLocalFile()) return fail(message("Choose a local image file."));
    iiSocietyHelper::FileSystem files(iiSocietyHelper::FileSystem::Initialization::Deferred);
    if (!files.openForSource(source.toLocalFile())) return fail(files.errorString());
    const auto original = files.aliasForPath(source.toLocalFile());
    if (!original.value("available").toBool()) return fail(message("Choose a readable local image."));
    const auto canonical = original.value("source").toUrl();
    for (const auto &entry : attachments_)
        if (entry.toMap().value("source").toUrl() == canonical) return fail({});
    if (attachments_.size() >= 19) return fail(message("Attach up to 19 images alongside the canvas."));
    auto alias = files.importCanvas(source.toLocalFile());
    if (alias.isEmpty()) return fail(files.errorString());
    const auto preview = files.preview(alias.value("path").toString());
    if (preview.isEmpty()) return fail(files.errorString());
    alias["previewSource"] = preview; alias["thumbnailSource"] = preview;
    QImageReader reader(preview.toLocalFile()); reader.setAutoTransform(true);
    const auto size = reader.size();
    if (original.value("size").toLongLong() > 256LL * 1024 * 1024 || !reader.canRead() || size.isEmpty() ||
        qint64(size.width()) * size.height() > 64LL * 1024 * 1024)
        return fail(message("Choose a readable image of at most 64 megapixels."));
    attachments_.append(QVariantMap{{"source", canonical}, {"filename", original.value("name")},
        {"alias", alias}, {"documentSource", alias.value("source")}, {"previewSource", preview},
        {"width", size.width()}, {"height", size.height()}, {"sizeBytes", original.value("size")}});
    return fail({});
}
bool HomeReferenceStore::remove(int index) {
    if (index < 0 || index >= attachments_.size()) return fail(message("Attachment no longer exists."));
    attachments_.removeAt(index); return fail({});
}
bool HomeReferenceStore::removeKey(const QString &wanted) {
    if (!wanted.isEmpty())
        for (qsizetype index = 0; index < attachments_.size(); ++index)
            if (key(attachments_.at(index).toMap()) == wanted) return remove(static_cast<int>(index));
    return fail(message("Attachment no longer exists."));
}
QVariantList HomeReferenceStore::previewSources() const {
    QVariantList result; result.reserve(attachments_.size());
    for (const auto &entry : attachments_) result.append(entry.toMap().value("previewSource").toUrl().toString());
    return result;
}
} // namespace dreamscapes
