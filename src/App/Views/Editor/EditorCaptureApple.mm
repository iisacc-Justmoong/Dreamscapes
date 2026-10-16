#include "EditorCaptureNative.h"
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>
#include <cmath>
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>

namespace {
QVariantList displayTargets() {
    CGDirectDisplayID ids[32]; uint32_t count = 0; QVariantList result;
    if (CGGetActiveDisplayList(32, ids, &count) != kCGErrorSuccess) return result;
    for (uint32_t i = 0; i < count; ++i) {
        const auto bounds = CGDisplayBounds(ids[i]);
        const auto label = ids[i] == CGMainDisplayID() ? QStringLiteral("Main display") : QStringLiteral("Display %1").arg(i + 1);
        result.append(QVariantMap{{"id", QString::number(ids[i])}, {"label", label}, {"kind", "Screen"},
            {"x", bounds.origin.x}, {"y", bounds.origin.y}, {"width", bounds.size.width}, {"height", bounds.size.height},
            {"pixelWidth", int(CGDisplayPixelsWide(ids[i]))}, {"pixelHeight", int(CGDisplayPixelsHigh(ids[i]))}});
    }
    return result;
}
QString nativeError(NSError *error) { return error ? QString::fromNSString(error.localizedDescription) : QStringLiteral("Enable Screen Recording for Dreamscapes in System Settings."); }
QImage guiImage(CGImageRef image) {
    NSMutableData *data = [NSMutableData data];
    CGImageDestinationRef destination = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data, CFSTR("public.png"), 1, nullptr);
    if (!destination) return {};
    CGImageDestinationAddImage(destination, image, nullptr);
    const bool success = CGImageDestinationFinalize(destination); CFRelease(destination);
    return success ? QImage::fromData(static_cast<const uchar *>(data.bytes), int(data.length), "PNG") : QImage{};
}
}
bool editorNativeScreenCaptureSupported() { if (@available(macOS 14.0, *)) return true; return false; }
void editorCaptureTargets(CaptureTargetsReply reply) {
    const auto displays = displayTargets();
    if (!CGPreflightScreenCaptureAccess()) { reply(displays, {}); return; }
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:YES completionHandler:^(SCShareableContent *content, NSError *error) {
        if (error) { reply(displays, nativeError(error)); return; }
        auto targets = displays;
        for (SCWindow *window in content.windows) {
            if (!window.onScreen || window.frame.size.width < 32 || window.frame.size.height < 32 || window.title.length == 0) continue;
            targets.append(QVariantMap{{"id", QString::number(window.windowID)},
                {"label", QString::fromNSString([NSString stringWithFormat:@"%@ · %@", window.owningApplication.applicationName, window.title])},
                {"kind", "Window"}, {"x", window.frame.origin.x}, {"y", window.frame.origin.y},
                {"width", window.frame.size.width}, {"height", window.frame.size.height}});
        }
        reply(targets, {});
    }];
}
void editorCaptureImage(const QVariantMap &settings, CaptureImageReply reply) {
    if (@available(macOS 14.0, *)) {
        if (!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess()) {
            reply({}, QStringLiteral("Enable Screen Recording for Dreamscapes in System Settings, then capture again.")); return;
        }
        [SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:YES completionHandler:^(SCShareableContent *content, NSError *error) {
            if (error || !content) { reply({}, nativeError(error)); return; }
            const bool windowMode = settings.value("captureScope", "Screen").toString() == "Window";
            const auto target = settings.value("captureTarget").toString().toUInt();
            SCContentFilter *filter = nil; CGRect bounds = CGRectZero; double scale = 1;
            if (windowMode) {
                for (SCWindow *window in content.windows) if (window.windowID == target) {
                    filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window]; bounds = window.frame; break;
                }
                for (SCDisplay *display in content.displays) if (CGRectContainsPoint(display.frame, CGPointMake(CGRectGetMidX(bounds), CGRectGetMidY(bounds)))) {
                    scale = display.width / display.frame.size.width; break;
                }
            } else {
                SCDisplay *selected = nil;
                for (SCDisplay *display in content.displays) if (display.displayID == target || (!target && display.displayID == CGMainDisplayID())) selected = display;
                if (selected) {
                    filter = [[SCContentFilter alloc] initWithDisplay:selected excludingWindows:@[]];
                    bounds = selected.frame; scale = selected.width / bounds.size.width;
                }
            }
            if (!filter || bounds.size.width <= 0 || bounds.size.height <= 0) {
                reply({}, QStringLiteral("Choose an available display or window. Refresh the target list if it has closed.")); return;
            }
            SCStreamConfiguration *config = [SCStreamConfiguration new];
            const auto full = settings.value("captureFullResolution", true).toBool();
            config.width = MAX(1, lround(bounds.size.width * (full ? scale : 1)));
            config.height = MAX(1, lround(bounds.size.height * (full ? scale : 1)));
            config.showsCursor = settings.value("capturePointer", true).toBool();
            config.ignoreShadowsSingleWindow = YES;
            CGColorRef background = CGColorCreateGenericRGB(0, 0, 0, windowMode && settings.value("captureTransparent").toBool() ? 0 : 1);
            config.backgroundColor = background; CGColorRelease(background);
            [SCScreenshotManager captureImageWithFilter:filter configuration:config completionHandler:^(CGImageRef image, NSError *captureError) {
                if (captureError || !image) { reply({}, nativeError(captureError)); return; }
                auto pixels = guiImage(image);
                if (settings.value("captureScope").toString() == "Region") {
                    const auto region = settings.value("captureRegion").toList();
                    if (region.size() != 4) { reply({}, QStringLiteral("Select a region before capturing.")); return; }
                    const double sx = pixels.width() / bounds.size.width, sy = pixels.height() / bounds.size.height;
                    const QRect crop(qRound((region[0].toDouble() - bounds.origin.x) * sx), qRound((region[1].toDouble() - bounds.origin.y) * sy),
                        qRound(region[2].toDouble() * sx), qRound(region[3].toDouble() * sy));
                    const auto bounded = crop.intersected(pixels.rect());
                    if (bounded.isEmpty()) { reply({}, QStringLiteral("The selected region is outside this display.")); return; }
                    pixels = pixels.copy(bounded);
                }
                reply(pixels, {});
            }];
        }];
    } else reply({}, QStringLiteral("Screen capture requires macOS 14 or later."));
}
#else
bool editorNativeScreenCaptureSupported() { return false; }
void editorCaptureTargets(CaptureTargetsReply reply) { reply({}, {}); }
void editorCaptureImage(const QVariantMap &, CaptureImageReply reply) { reply({}, QStringLiteral("Screen capture is unavailable on this platform.")); }
#endif

