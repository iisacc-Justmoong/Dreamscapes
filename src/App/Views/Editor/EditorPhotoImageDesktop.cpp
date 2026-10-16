#include "EditorPhotoImageNative.h"
#include <QImageReader>
QImage editorDecodeOriginalPhoto(const QString &path) { QImageReader reader(path); reader.setAutoTransform(true); return reader.read(); }

