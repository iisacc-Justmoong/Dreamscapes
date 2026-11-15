#pragma once

#include <QSize>
#include <QString>
#include <QUrl>
#include <QVariant>

namespace dreamscapes {
// Shared attachment preparation for both the retained canvas and native Home.
class HomeReferenceStore final {
  public:
    [[nodiscard]] const QVariantList &attachments() const { return attachments_; }
    [[nodiscard]] QString error() const { return error_; }
    bool add(const QUrl &source);
    bool remove(int index);
    bool removeKey(const QString &key);
    [[nodiscard]] QVariantList previewSources() const;
    [[nodiscard]] static QString key(const QVariantMap &entry);
  private:
    bool fail(QString);
    QVariantList attachments_;
    QString error_;
};
[[nodiscard]] QSize homeReferenceOutputSize(const QString &ratio);
} // namespace dreamscapes
