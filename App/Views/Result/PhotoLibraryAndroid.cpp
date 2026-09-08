#include "PhotoLibraryNative.h"
#include <QCoreApplication>
#include <QHash>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMimeDatabase>

namespace {
constexpr char activityClass[] = "com/iisacc/dreamscapes/DreamscapesActivity";
QHash<jlong, PhotoLibraryExporter::Completion> pending;
jlong nextRequest = 0;

void photoSaveFinished(JNIEnv *, jclass, jlong request, jstring asset, jstring error)
{
    const auto identifier = QJniObject(asset).toString();
    const auto message = QJniObject(error).toString();
    QMetaObject::invokeMethod(qApp, [request, identifier, message] {
        if (auto completion = pending.take(request)) completion(identifier, message);
    }, Qt::QueuedConnection);
}
}

PhotoLibraryExporter::SaveOperation nativePhotoLibrarySaveOperation()
{
    return [](const QString &path, PhotoLibraryExporter::Completion completion) {
        QJniEnvironment environment;
        static const bool registered = environment.registerNativeMethods(activityClass, {
            {"photoSaveFinished", "(JLjava/lang/String;Ljava/lang/String;)V",
             reinterpret_cast<void *>(photoSaveFinished)}
        });
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!registered || !QNativeInterface::QAndroidApplication::isActivityContext()
            || !activity.isValid()) {
            completion({}, QStringLiteral("Could not start the photo library service."));
            return;
        }
        const auto request = ++nextRequest;
        const auto method = environment->GetMethodID(activity.objectClass(), "savePhoto",
            "(Ljava/lang/String;Ljava/lang/String;J)V");
        if (environment.checkAndClearExceptions() || !method) {
            completion({}, QStringLiteral("The photo library service is unavailable."));
            return;
        }
        pending.insert(request, std::move(completion));
        const auto source = QJniObject::fromString(path);
        const auto mime = QJniObject::fromString(QMimeDatabase().mimeTypeForFile(path).name());
        environment->CallVoidMethod(activity.object(), method,
                                    source.object<jstring>(), mime.object<jstring>(), request);
        if (environment.checkAndClearExceptions())
            pending.take(request)({}, QStringLiteral("Could not start saving to Photos."));
    };
}
