#include "EditorPhotoImageNative.h"
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

QImage editorDecodeOriginalPhoto(const QString &path) {
    @autoreleasepool {
        NSURL *url = [NSURL fileURLWithPath:path.toNSString()];
        CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
        if (!source) return {};
        NSDictionary *properties = CFBridgingRelease(CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr));
        const int width = [properties[(__bridge NSString *)kCGImagePropertyPixelWidth] intValue];
        const int height = [properties[(__bridge NSString *)kCGImagePropertyPixelHeight] intValue];
        // This API applies EXIF orientation; maximum size is the ORIGINAL size.
        NSDictionary *options = @{(__bridge NSString *)kCGImageSourceCreateThumbnailFromImageAlways: @YES,
            (__bridge NSString *)kCGImageSourceCreateThumbnailWithTransform: @YES,
            (__bridge NSString *)kCGImageSourceThumbnailMaxPixelSize: @(MAX(width, height))};
        CGImageRef image = CGImageSourceCreateThumbnailAtIndex(source, 0, (__bridge CFDictionaryRef)options); CFRelease(source);
        if (!image) return {};
        NSMutableData *data = [NSMutableData data];
        CGImageDestinationRef destination = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data, CFSTR("public.png"), 1, nullptr);
        if (!destination) { CGImageRelease(image); return {}; }
        CGImageDestinationAddImage(destination, image, nullptr);
        const bool saved = CGImageDestinationFinalize(destination); CFRelease(destination); CGImageRelease(image);
        return saved ? QImage::fromData(static_cast<const uchar *>(data.bytes), int(data.length), "PNG") : QImage{};
    }
}

