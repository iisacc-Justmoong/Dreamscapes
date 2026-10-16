#pragma once
#include <QImage>
#include <QString>

// Full original pixels for native photo codecs absent from Qt image plugins.
QImage editorDecodeOriginalPhoto(const QString &path);

