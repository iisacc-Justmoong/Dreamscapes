#include "PhotoLibraryNative.h"
#import <Foundation/Foundation.h>
#import <Photos/Photos.h>

PhotoLibraryExporter::SaveOperation nativePhotoLibrarySaveOperation()
{
    if (@available(macOS 11.0, iOS 14.0, *)) {
        return [](const QString &path, PhotoLibraryExporter::Completion completion) {
            NSURL *url = [NSURL fileURLWithPath:path.toNSString()];
            auto importPhoto = ^(PHAuthorizationStatus status) {
                if (status != PHAuthorizationStatusAuthorized && status != PHAuthorizationStatusLimited) {
                    completion({}, QStringLiteral("Allow Dreamscapes to add photos in system settings, then try again."));
                    return;
                }
                __block NSString *identifier = nil;
                [[PHPhotoLibrary sharedPhotoLibrary] performChanges:^{
                    PHAssetCreationRequest *request = [PHAssetCreationRequest creationRequestForAsset];
                    // Import the original resource, preserving resolution and metadata.
                    [request addResourceWithType:PHAssetResourceTypePhoto fileURL:url options:nil];
                    identifier = request.placeholderForCreatedAsset.localIdentifier;
                } completionHandler:^(BOOL success, NSError *error) {
                    if (success) completion(QString::fromNSString(identifier), {});
                    else completion({}, QStringLiteral("Could not save to Photos: %1").arg(QString::fromNSString(error.localizedDescription)));
                }];
            };
            const auto status = [PHPhotoLibrary authorizationStatusForAccessLevel:PHAccessLevelAddOnly];
            if (status == PHAuthorizationStatusNotDetermined)
                [PHPhotoLibrary requestAuthorizationForAccessLevel:PHAccessLevelAddOnly handler:importPhoto];
            else importPhoto(status);
        };
    }
    return {};
}
