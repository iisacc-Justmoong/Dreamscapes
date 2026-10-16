#pragma once
#include <QImage>
#include <QVariantMap>
#include <functional>

// Native screen images cross this boundary as GUI pixels. No app/document state.
using CaptureTargetsReply = std::function<void(QVariantList, QString)>;
using CaptureImageReply = std::function<void(QImage, QString)>;
void editorCaptureTargets(CaptureTargetsReply);
void editorCaptureImage(const QVariantMap &, CaptureImageReply);
bool editorNativeScreenCaptureSupported();

