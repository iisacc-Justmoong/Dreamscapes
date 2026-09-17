#pragma once
#include <Generation/NativeDiffusion.hpp>
#include <QtGlobal>
#include <algorithm>
#include <limits>

// Actual completed engine units, including repeated loading/refinement passes.
// The outstanding count is an estimate until the final image is saved.
class GenerationWorkProgress {
public:
    void update(const iiLocalDiffusion::NativeGenerationProgress &event) {
        using Stage = iiLocalDiffusion::NativeGenerationStage;
        if (event.stage == Stage::Waiting || event.step < 0 || event.total < 0
            || (event.total > 0 && event.step > event.total)) return;
        const bool computing = event.stage == Stage::Computing;
        auto &previous = computing ? m_computed : m_previous;
        const bool samePass = computing || (m_stage == event.stage && m_total == event.total);
        const qint64 delta = samePass && event.step >= previous ? event.step - previous : event.step;
        previous = event.step;
        if (!computing) { m_stage = event.stage; m_total = event.total; }
        const auto maximum = std::numeric_limits<qint64>::max();
        m_completed += std::min(delta, maximum - 1 - m_completed);
        if (!computing) m_remaining = std::max<qint64>(1, qint64(event.total) - event.step);
    }
    qint64 completed() const { return m_completed; }
    qint64 total() const {
        return m_completed + std::min(m_remaining, std::numeric_limits<qint64>::max() - m_completed);
    }
private:
    iiLocalDiffusion::NativeGenerationStage m_stage = iiLocalDiffusion::NativeGenerationStage::Waiting;
    int m_previous = 0, m_computed = 0, m_total = 0;
    qint64 m_completed = 0, m_remaining = 1;
};
