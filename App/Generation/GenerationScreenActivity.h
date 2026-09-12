#pragma once
#include <functional>

std::function<void(bool)> nativeGenerationScreenActivity();
#ifdef DREAMSCAPES_LOCAL_RUNTIME_PROBE
#include <QVariantMap>
QVariantMap nativeGenerationScreenStatus();
#endif
