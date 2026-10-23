#pragma once
#include <iiSocietyHelper.h>
#include <Generation/NativeDiffusion.hpp>
#include <QByteArray>
#include <QJsonObject>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace dreamscapes {
// Cancellation has a precise boundary: preparation may be retired, while an
// accepted file commit reports its actual outcome instead of a late cancellation.
class PublicationControl final {
public:
    enum class Phase { Preparing, Cancelled, Committing, Finished };
    bool cancel() noexcept {
        auto expected = Phase::Preparing;
        return phase.compare_exchange_strong(expected, Phase::Cancelled) || expected == Phase::Cancelled;
    }
    bool cancelled() const noexcept { return phase.load() == Phase::Cancelled; }
    bool beginCommit() noexcept {
        auto expected = Phase::Preparing;
        return phase.compare_exchange_strong(expected, Phase::Committing);
    }
    void finish() noexcept { phase.store(Phase::Finished); }
private:
    std::atomic<Phase> phase{Phase::Preparing};
};
enum class PublicationOrigin { Worker, Native, Remote };
struct GenerationPublicationInput {
    PublicationOrigin origin = PublicationOrigin::Worker;
    QString root, containerId, directory, output;
    QJsonObject job;
    std::optional<iiLocalDiffusion::NativeGenerationResult> native;
    QByteArray remotePng;
    std::shared_ptr<void> lifetime;
    std::shared_ptr<PublicationControl> control;
};
struct GenerationPublicationResult {
    QJsonObject values;
    iiSocietyHelper::PreparedStorage storage;
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled && storage.isValid(); }
};
using GenerationPublicationOperation = std::function<GenerationPublicationResult(GenerationPublicationInput)>;
// Worker-only codec/storage boundary. It retains values and a source lease,
// never a controller, view, or GUI-owned filesystem adapter.
GenerationPublicationResult publishGenerationResult(GenerationPublicationInput input);
}
