#pragma once

#include <Generation/NativeDiffusion.hpp>
#include <QString>
#include <QVariantMap>
#include <functional>
#include <memory>

// Platform execution permission is independent of window focus and screen wake.
// All methods and callbacks run on the controller's main thread.
class GenerationBackgroundActivity {
public:
    virtual ~GenerationBackgroundActivity() = default;
    virtual void observeForeground(std::function<void(bool)> changed) = 0;
    virtual void begin(const QString &job, std::function<void()> expired) = 0;
    virtual bool allowsBackgroundExecution() const = 0;
    virtual void update(const iiLocalDiffusion::NativeGenerationProgress &progress) = 0;
    virtual void end(bool success) = 0;
    virtual QVariantMap status() const = 0;
};

std::shared_ptr<GenerationBackgroundActivity> nativeGenerationBackgroundActivity();
