#include "GenerationPresentation.h"
#include <map>
#include <regex>
#include <set>

namespace dreamscapes::vm {
bool generationPending(std::string_view state) {
    return state == "queued" || state == "running" || state == "connecting-host" || state == "downloading";
}
std::string generationStateLabel(std::string_view state) {
    static const std::map<std::string, std::string, std::less<>> labels{
        {"queued", "Queued"},
        {"running", "Generating"},
        {"completed", "Completed"},
        {"failed", "Failed"},
        {"cancelled", "Cancelled"},
        {"interrupted", "Interrupted"},
        {"downloading", "Downloading model"},
        {"connecting-host", "Connecting to Society host"}};
    const auto found = labels.find(state);
    return found == labels.end() ? std::string(state) : found->second;
}
Value generationPresentation(const GenerationSnapshot &input) {
    Array jobs, results, reviewResults = input.connected ? array(input.results) : Array{};
    Value active(Object{}), reviewActive(Object{}), selectedResult(Object{});
    bool pending = false, reviewPending = false;
    std::string failure;
    std::set<std::string> identifiers;
    for (const auto &value : array(input.requestIds))
        identifiers.insert(text(value));
    for (const auto &job : array(input.jobs)) {
        const auto state = text(get(job, "state"));
        const bool inFlight = generationPending(state);
        reviewPending |= inFlight;
        if (inFlight && state != "queued" && object(reviewActive).empty())
            reviewActive = job;
        if (!identifiers.contains(text(get(job, "id"))))
            continue;
        jobs.push_back(job);
        pending |= inFlight;
        if (inFlight && state != "queued" && object(active).empty())
            active = job;
        if (state == "failed" && failure.empty())
            failure = text(get(job, "error"));
    }
    if (input.connected)
        for (const auto &result : array(input.results))
            if (identifiers.contains(text(get(result, "id"))))
                results.push_back(result);
    const auto source = [](const Value &value) {
        for (auto key : {"documentSource", "mediaSource", "imageSource"})
            if (!text(get(value, key)).empty())
                return text(get(value, key));
        return std::string();
    };
    if (identifiers.empty() && !text(get(input.currentResult, "imageSource")).empty()) {
        const auto current = source(input.currentResult);
        if (std::ranges::none_of(reviewResults, [&](const auto &v) { return source(v) == current; }))
            reviewResults.push_back(input.currentResult);
    }
    for (const auto &result : results)
        if (text(get(result, "id")) == input.selectedResultId)
            selectedResult = result;
    if (!input.selectedImageSource.empty())
        for (const auto &result : results)
            if (text(get(result, "imageSource")) == input.selectedImageSource) {
                selectedResult = result;
                break;
            }
    if (object(selectedResult).empty() && !results.empty())
        selectedResult = results.back();
    std::string status;
    if (object(active).empty()) {
        if (pending) {
            const auto state = text(get(input.inferenceStatus, "state"));
            status = !input.busy && state == "checking-model"                      ? "Checking model…"
                     : !input.busy && (state == "preparing" || state == "loading") ? "Preparing model…"
                                                                                   : "Queued";
        } else if (!jobs.empty()) {
            const auto state = text(get(jobs.front(), "state"));
            if (state == "cancelled" || state == "interrupted")
                status = generationStateLabel(state);
        }
    } else {
        const auto phase = text(get(input.inferenceStatus, "state"));
        const bool video = text(get(active, "mediaType")) == "Video";
        std::map<std::string, std::string, std::less<>> labels{
            {"interpolating", "Interpolating video frames…"},
            {"encoding-video", "Encoding video…"},
            {"complete", "Saving video…"},
            {"preparing-model", "Preparing model for faster generation…"},
            {"waiting-host", "Connecting to Society host…"},
            {"remote-queued", "Queued on Society host…"},
            {"remote-running", "Generating on Society host…"},
            {"remote-cancelling", "Stopping generation on host…"},
            {"receiving-image", "Receiving generated image…"},
            {"loading", "Loading model…"},
            {"encoding", "Preparing prompt…"},
            {"decoding", video ? "Rendering video frames…" : "Rendering image…"},
            {"cancelling", "Stopping generation…"},
            {"paused", "Paused — return to Dreamscapes to continue"},
            {"waiting-engine", "Waiting for image engine…"}};
        const auto percent =
            static_cast<int>(std::floor(100 * number(get(input.inferenceStatus, "completedBytes")) /
                                        std::max(1.0, number(get(input.inferenceStatus, "totalBytes"), 1))));
        labels["downloading-model"] = "Downloading model… " + std::to_string(percent) + '%';
        labels["checking-model"] = "Checking model… " + std::to_string(percent) + '%';
        const bool finished = input.previewTotalSteps > 0 && input.previewStep == input.previewTotalSteps;
        if (phase == "cancelling" || phase == "paused")
            status = labels[phase];
        else if (phase == "decoding" || (finished && text(get(input.inferenceStatus, "backend")) == "native"))
            status = labels["decoding"];
        else if (video && (phase == "interpolating" || phase == "encoding-video" || phase == "complete"))
            status = labels[phase];
        else if (input.previewTotalSteps > 0 || input.previewStep > 0) {
            const auto total = number(get(input.inferenceStatus, "total"));
            const bool refining = input.previewTotalSteps > 0 && total > 0 && total < number(get(active, "steps"));
            status = std::string(refining ? "Refining " : "Denoising ") + std::to_string(input.previewStep) + " / " +
                     std::to_string(input.previewTotalSteps);
        } else
            status = labels.contains(phase) ? labels[phase] : "Preparing generation…";
        status += " · " + std::to_string(input.elapsedSeconds) + " s";
    }
    return Value(Object{{"submissionJobs", Value(jobs)},
                        {"submissionResults", Value(results)},
                        {"reviewResults", Value(reviewResults)},
                        {"activeGeneration", active},
                        {"reviewActiveJob", reviewActive},
                        {"generationPending", Value(pending)},
                        {"reviewPending", Value(reviewPending)},
                        {"selectedResult", selectedResult},
                        {"failure", Value(failure)},
                        {"generationStatusText", Value(status)}});
}
Value historyEntry(const Value &entry) {
    const auto name = text(get(entry, "name"));
    static const std::regex poster("^[0-9a-f-]{36}-poster\\.png$");
    if (!std::regex_match(name, poster))
        return entry;
    auto result = object(entry);
    result["mediaType"] = Value("Video");
    result["imageSource"] = get(entry, "previewSource");
    const auto source = text(get(entry, "previewSource"));
    const auto query = source.find('?');
    const auto path = source.substr(0, query);
    const auto at = path.rfind("-poster.png");
    result["mediaSource"] = Value(at == std::string::npos ? source : path.substr(0, at) + "-0001.mp4");
    result["name"] = Value(name.substr(0, name.size() - 11) + "-0001.mp4");
    result["metadata1"] = Value("MP4 · Video");
    return Value(std::move(result));
}
Value searchEntries(const Value &files, const Value &published, const Value &history) {
    Array result;
    for (const auto &group :
         {std::pair{&files, "Files"}, std::pair{&published, "Published"}, std::pair{&history, "History"}}) {
        for (const auto &entry : array(*group.first)) {
            result.emplace_back(Object{{"file", entry},
                                       {"label", Value(std::string(group.second) + " · " + text(get(entry, "name")))}});
            if (result.size() == 8)
                return Value(std::move(result));
        }
    }
    return Value(std::move(result));
}
} // namespace dreamscapes::vm
