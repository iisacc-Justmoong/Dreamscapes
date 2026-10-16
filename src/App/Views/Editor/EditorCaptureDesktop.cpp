#include "EditorCaptureNative.h"
#include <QGuiApplication>
#include <QScreen>
#include <QPixmap>
#include <QTimer>

bool editorNativeScreenCaptureSupported() {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    return false;
#else
    return true;
#endif
}
void editorCaptureTargets(CaptureTargetsReply reply) {
    QVariantList targets;
    for (const auto *screen : QGuiApplication::screens()) {
        const auto rect = screen->geometry(); const auto ratio = screen->devicePixelRatio();
        targets.append(QVariantMap{{"id", screen->name()}, {"label", screen == QGuiApplication::primaryScreen() ? QStringLiteral("Main display") : screen->name()},
            {"kind", "Screen"}, {"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()},
            {"pixelWidth", qRound(rect.width() * ratio)}, {"pixelHeight", qRound(rect.height() * ratio)}});
    }
    reply(targets, {});
}
void editorCaptureImage(const QVariantMap &settings, CaptureImageReply reply) {
    if (!editorNativeScreenCaptureSupported()) { reply({}, QStringLiteral("Screen capture is unavailable on this platform.")); return; }
    if (settings.value("captureScope").toString() == "Window") { reply({}, QStringLiteral("Window capture is unavailable in this platform backend.")); return; }
    QScreen *selected = QGuiApplication::primaryScreen();
    for (auto *screen : QGuiApplication::screens()) if (screen->name() == settings.value("captureTarget").toString()) selected = screen;
    if (!selected) { reply({}, QStringLiteral("No display is available.")); return; }
    auto pixels = selected->grabWindow(0).toImage();
    if (settings.value("captureScope").toString() == "Region") {
        const auto region = settings.value("captureRegion").toList();
        if (region.size() != 4) { reply({}, QStringLiteral("Select a region before capturing.")); return; }
        const double ratio = selected->devicePixelRatio(); const auto origin = selected->geometry().topLeft();
        pixels = pixels.copy(QRect(qRound((region[0].toDouble() - origin.x()) * ratio), qRound((region[1].toDouble() - origin.y()) * ratio), qRound(region[2].toDouble() * ratio), qRound(region[3].toDouble() * ratio)).intersected(pixels.rect()));
    }
    if (!settings.value("captureFullResolution", true).toBool()) pixels = pixels.scaled(pixels.size() / selected->devicePixelRatio(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    reply(pixels, pixels.isNull() ? QStringLiteral("Screen access is unavailable. Check the operating system's recording permission.") : QString{});
}

