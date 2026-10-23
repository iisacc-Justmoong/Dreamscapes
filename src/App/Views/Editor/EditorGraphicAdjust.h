#pragma once
#include <QImage>
#include <QSize>
#include <QStringList>
#include <QVariantMap>
#include <vector>
#include <optional>
#include "EditorGraphicHelperValues.h"
#include <iiGraphicAdjust/Masks.h>

// Product boundary only. iiGraphicAdjust remains a Qt-free C++23 leaf SDK.
namespace EditorGraphicAdjust {
struct PreparedHelper {
    std::optional<graphicHelper::Request> request;
    QString error;
    bool ok() const { return request.has_value() && error.isEmpty(); }
};
[[nodiscard]] QStringList helpers();
[[nodiscard]] bool isHelper(const QString &operation);
[[nodiscard]] bool helperNeedsImage(const QString &operation);
[[nodiscard]] PreparedHelper prepareHelper(const QString &operation, const QVariantMap &parameters);
[[nodiscard]] QVariantMap helperParameters(const graphicHelper::Result &result);
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
// Views are borrowed only for the duration of the synchronous adapter call.
// They replace the corresponding top-level serialized parameters, never masks
// inside an explicit LocalAdjustment collection.
struct MaskInputs {
    std::optional<iiGraphicAdjust::MaskView> mask, base, operand;
};
// Detached synchronous processing. Callers own document writes, mask blending,
// cancellation, history and source snapshots. This function never mutates source.
[[nodiscard]] Result process(const QImage &source, const QString &operation, const QVariantMap &parameters = {});
[[nodiscard]] Result process(const QImage &source, const QString &operation, const QVariantMap &parameters, const MaskInputs &masks);
[[nodiscard]] MaskResult makeMask(const QImage &source, const QString &operation, const QVariantMap &parameters = {});
[[nodiscard]] MaskResult makeMask(const QImage &source, const QString &operation, const QVariantMap &parameters, const MaskInputs &masks);
// Geometry-only operations consume dimensions/masks, with no placeholder image.
[[nodiscard]] MaskResult makeMask(QSize sourceExtent, const QString &operation, const QVariantMap &parameters, const MaskInputs &masks = {});
[[nodiscard]] bool maskNeedsPixels(const QString &operation);
[[nodiscard]] QString canonicalOperation(const QString &operation);
[[nodiscard]] bool usesAdjustmentCollection(const QVariantMap &parameters);
[[nodiscard]] QStringList operations();
[[nodiscard]] QStringList parameterNames(const QString &operation);
[[nodiscard]] bool supports(const QString &operation);
} // namespace EditorGraphicAdjust
