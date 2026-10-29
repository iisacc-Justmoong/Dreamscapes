#pragma once

#include <iiMultimodalAgent/agent/Types.h>
#include <memory>
#include <stdexcept>
#include <utility>

namespace dreamscapes {

// Reports are stored in full. Only the copy supplied to an optional language
// model may be shortened, using that model's own tokenizer and context limit.
class FailureModelBudget final : public iiMultimodalAgent::agent::Model {
    using Model = iiMultimodalAgent::agent::Model;
    using ModelRequest = iiMultimodalAgent::agent::ModelRequest;
    using CancellationToken = iiMultimodalAgent::CancellationToken;
    std::shared_ptr<Model> delegate_;

    ModelRequest fit(ModelRequest request, const CancellationToken& token) const {
        auto budget = delegate_->measure(request, token);
        if (!budget || budget->inputTokens <= qint64(budget->contextTokens) - request.generation.maxTokens)
            return request;

        qsizetype index = request.messages.size();
        while (index > 0) {
            --index;
            const auto& message = request.messages[index];
            if (message.role == iiMultimodalAgent::agent::MessageRole::User && !message.text.isEmpty()
                && message.content.isEmpty()) break;
        }
        if (request.messages.isEmpty() || request.messages[index].role != iiMultimodalAgent::agent::MessageRole::User
            || request.messages[index].text.isEmpty() || !request.messages[index].content.isEmpty())
            throw std::runtime_error("The failure explanation has no text that can be reduced to fit the model.");

        const auto original = request.messages[index].text;
        const QString omission = "\n[Further evidence is omitted from model input; the full report is preserved.]";
        const auto fits = [&](qsizetype length) {
            if (length > 0 && length < original.size() && original[length - 1].isHighSurrogate()
                && original[length].isLowSurrogate()) --length;
            request.messages[index].text = original.left(length) + omission;
            const auto current = delegate_->measure(request, token);
            return current && current->inputTokens <= qint64(current->contextTokens) - request.generation.maxTokens;
        };
        if (!fits(0)) throw std::runtime_error("The selected model has insufficient context for a failure explanation.");
        qsizetype low = 0, high = original.size();
        while (low < high) {
            const auto middle = low + (high - low + 1) / 2;
            if (fits(middle)) low = middle; else high = middle - 1;
        }
        (void)fits(low);
        return request;
    }
public:
    explicit FailureModelBudget(std::shared_ptr<Model> delegate) : delegate_(std::move(delegate)) {
        if (!delegate_) throw std::invalid_argument("A failure explanation model is required.");
    }
    std::optional<iiMultimodalAgent::ContextBudget> measure(
        const ModelRequest& request, const CancellationToken& token) override {
        return delegate_->measure(fit(request, token), token);
    }
    iiMultimodalAgent::agent::ModelReply generate(const ModelRequest& request, const CancellationToken& token,
        const std::function<bool(const QString&)>& onDelta) override {
        return delegate_->generate(fit(request, token), token, onDelta);
    }
};

} // namespace dreamscapes
