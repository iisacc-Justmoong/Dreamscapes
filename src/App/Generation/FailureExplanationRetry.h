#pragma once
#include <iiMultimodalAgent/agent/Engine.h>

namespace dreamscapes {
// A truncated structured turn is never accepted as a result. A fresh,
// shorter attempt is the only recoverable case, and it is attempted once.
template <typename Attempt>
iiMultimodalAgent::agent::RunResult retryFailureExplanation(Attempt &&attempt) {
    using iiMultimodalAgent::agent::RunStatus;
    auto result = attempt(false);
    if (result.status == RunStatus::Failed && result.errorMessage.contains(
            QStringLiteral("Structured response reached the output limit before a complete turn")))
        return attempt(true);
    return result;
}
}
