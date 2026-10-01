#pragma once
#include <Generation/NativeDiffusion.hpp>
#include <QJsonArray>
#include <QString>

namespace dreamscapes {
// Decode on the generation worker, not the UI thread. Sources were canonicalized
// at submission; returned RGB remains owned until the synchronous native call ends.
bool decodeAdvancedReferences(const QJsonArray &sources,
    iiLocalDiffusion::NativeAdvancedControls *controls, QString *error,
    const std::atomic_bool &cancelled,
    const std::shared_ptr<iiLocalDiffusion::NativeExecutionControl> &control = {});
bool decodeAdvancedControlImages(const QJsonArray &sources,
    iiLocalDiffusion::NativeAdvancedControls *controls, QString *error,
    const std::atomic_bool &cancelled,
    const std::shared_ptr<iiLocalDiffusion::NativeExecutionControl> &control = {});
}
