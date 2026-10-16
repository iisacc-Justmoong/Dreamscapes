#pragma once
#include <QImage>
#include <QStringList>
#include <QVariantMap>
#include <vector>

// Product boundary only. iiGraphicAdjust remains a Qt-free C++23 leaf SDK.
namespace EditorGraphicAdjust {
struct Result {
    QImage image;
    QString error;
    QVariantMap parameters; // Settings returned by automatic correction/sample actions.
    [[nodiscard]] bool ok() const noexcept { return error.isEmpty() && !image.isNull(); }
};
struct MaskResult {
    int width = 0, height = 0;
    std::vector<float> coverage;
    QString error;
    [[nodiscard]] bool ok() const noexcept { return error.isEmpty() && width > 0 && height > 0 && coverage.size() == std::size_t(width) * height; }
};
// Detached synchronous processing. Callers own document writes, mask blending,
// cancellation, history and source snapshots. This function never mutates source.
[[nodiscard]] Result process(const QImage &source, const QString &operation, const QVariantMap &parameters = {});
[[nodiscard]] MaskResult makeMask(const QImage &source, const QString &operation, const QVariantMap &parameters = {});
[[nodiscard]] QString canonicalOperation(const QString &operation);
[[nodiscard]] QStringList operations();
[[nodiscard]] QStringList parameterNames(const QString &operation);
[[nodiscard]] bool supports(const QString &operation);
} // namespace EditorGraphicAdjust
