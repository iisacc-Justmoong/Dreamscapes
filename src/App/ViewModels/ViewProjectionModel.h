#pragma once
#include "Native/ViewProjection.h"
#include <QObject>
#include <QVariant>

namespace dreamscapes {
// Qt is only the view adapter. Every projection is a compiled iiSDK query;
// range/lookup reconstruct original payloads rather than serializing them.
class ViewProjectionModel final : public QObject {
    Q_OBJECT
  public:
    explicit ViewProjectionModel(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE QStringList labels(const QVariantList &, const QString &key = QStringLiteral("label"),
                                   const QString &fallback = {}, int selected = -1) const;
    Q_INVOKABLE QVariantList values(const QVariantList &, const QString &key = {}) const;
    Q_INVOKABLE QVariantList choices(const QVariantList &, const QString &labelKey = QStringLiteral("name"),
                                    const QString &valueKey = QStringLiteral("id"),
                                    const QString &valueRole = QStringLiteral("value"),
                                    const QString &fallback = QStringLiteral("id")) const;
    Q_INVOKABLE int indexOf(const QVariantList &, const QVariant &, const QString &key = {}) const;
    Q_INVOKABLE QVariantList matching(const QVariantList &, const QVariant &, const QString &key) const;
    Q_INVOKABLE QVariantMap find(const QVariantList &, const QVariant &, const QString &key) const;
    Q_INVOKABLE QVariantList range(const QVariantList &, int begin = 0, int end = 2147483647) const;
    Q_INVOKABLE QVariantList concatenate(const QVariantList &, const QVariantList &) const;
    Q_INVOKABLE QVariantList prepend(const QVariant &entry, const QVariantList &rows) const;
    Q_INVOKABLE QString join(const QVariantList &, const QString &separator) const;
    Q_INVOKABLE QVariantList countChoices(int current) const;
    Q_INVOKABLE QStringList videoCountLabels(const QVariantList &, const QString &custom) const;
    Q_INVOKABLE QStringList notifications(const QVariantList &) const;
    Q_INVOKABLE QVariantMap timelineLayout(const QVariantList &, const QString &mode) const;
    Q_INVOKABLE QString timecode(int frame, double fps) const;
    Q_INVOKABLE QStringList trackLabels(const QVariantList &) const;
    Q_INVOKABLE QStringList videoResultLabels(const QVariantList &) const;
    Q_INVOKABLE QStringList sizeChoices(const QVariantList &, const QString &ratio) const;
    Q_INVOKABLE QVariant nextChoice(const QVariantList &, const QVariant &) const;
    Q_INVOKABLE QStringList filenames(const QVariantList &) const;
    Q_INVOKABLE int rulerStep(double pixelsPerFrame) const;
    Q_INVOKABLE QString fileField(const QVariantMap &, const QVariantMap &, const QString &, const QString &) const;
  private:
    QVariant read(vm::ProjectionRead, std::initializer_list<QVariant>) const;
    vm::ViewProjection projection_;
};
} // namespace dreamscapes
