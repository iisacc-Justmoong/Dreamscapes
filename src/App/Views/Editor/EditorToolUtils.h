#pragma once
#include <iiSharedCanvas.h>
#include <QBuffer>
#include <QImage>
#include <QPainterPath>
#include <QRegularExpression>
#include <QUuid>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <array>
#include <numbers>
namespace editorTools {
inline QString key(int n) { return QString("field-%1").arg(n); }
inline double number(const QVariantMap &v, int n, double fallback = 0) { return v.value(key(n), fallback).toDouble(); }
inline QString text(const QVariantMap &v, int n, const QString &fallback = {}) { return v.value(key(n), fallback).toString(); }
inline bool toggle(const QVariantMap &v, int n, bool fallback = false) { return v.value(key(n), fallback).toBool(); }
inline std::string unique(const char *prefix) { return std::string(prefix) + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(); }
inline QImage image(const RasterLayer &p) { return QImage(reinterpret_cast<const uchar *>(p.pixels.data()), p.width, p.height, p.width * 4, QImage::Format_ARGB32).copy(); }
inline RasterLayer raster(const QImage &input) {
    auto converted = input.convertToFormat(QImage::Format_ARGB32); auto result = makeRasterLayer(converted.width(), converted.height());
    for (int y = 0; y < converted.height(); ++y) std::copy_n(reinterpret_cast<const std::uint32_t *>(converted.constScanLine(y)), converted.width(), result.pixels.begin() + y * result.width);
    return result;
}
inline QUrl dataImage(const QImage &input) { QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); input.save(&buffer, "PNG"); return QUrl("data:image/png;base64," + QString::fromLatin1(bytes.toBase64())); }
inline iiSharedCanvas::VectorPath nativePath(const QPainterPath &p, std::uint32_t color) {
    using namespace iiSharedCanvas; VectorPath path; path.fill = SolidPaint{color};
    for (int i = 0; i < p.elementCount(); ++i) { const auto e = p.elementAt(i);
        if (e.isMoveTo()) path.commands.emplace_back(MoveTo{{e.x, e.y}});
        else if (e.isLineTo()) path.commands.emplace_back(LineTo{{e.x, e.y}});
        else if (e.type == QPainterPath::CurveToElement && i + 2 < p.elementCount()) {
            const auto c = p.elementAt(i + 1), end = p.elementAt(i + 2);
            path.commands.emplace_back(CubicTo{{e.x, e.y}, {c.x, c.y}, {end.x, end.y}}); i += 2;
        }
    }
    return path;
}
inline double seconds(const QString &input, bool *ok = nullptr) {
    const auto parts = input.trimmed().split(':'); double total = 0; bool valid = !parts.isEmpty() && parts.size() <= 3;
    for (const auto &part : parts) { bool numberOk; const auto n = part.toDouble(&numberOk); valid = valid && numberOk && std::isfinite(n) && n >= 0; total = total * 60 + n; }
    if (ok) *ok = valid; return valid ? total : -1;
}
inline std::pair<double, double> timeRange(const QString &s) { const auto parts = s.split(QRegularExpression("\\s*[—–]\\s*")); if (parts.size() != 2) return {-1, -1}; return {seconds(parts[0]), seconds(parts[1])}; }
inline RasterBlendMode blend(const QString &s) { return s == "Multiply" ? RasterBlendMode::Multiply : s == "Screen" ? RasterBlendMode::Screen : s == "Overlay" ? RasterBlendMode::Overlay : RasterBlendMode::SourceOver; }
}
