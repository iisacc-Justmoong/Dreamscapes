#include "App/Views/Home/NativeHomeQueue.h"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace dreamscapes;
using namespace dreamscapes::home;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
vm::Value job(const char *state) { return vm::Value(vm::Object{{"state", vm::Value(state)}}); }
void exercise() {
    HomeQueueModel model;
    check(model.status.get() == "Ready to generate", "empty queue status");
    check(model.summary.get() == "0 running · 0 waiting · 0 failed", "empty queue counts");
    check(model.device.get() == "Automatic" && model.model.get() == "Unavailable", "empty runtime values");
    check(model.execution.get() == "Idle" && !model.canOpenQueue.get(), "empty action state");
    HomeQueueSnapshot snapshot;
    snapshot.jobs = vm::Value(vm::Array{job("running"),job("connecting-host"),job("downloading"),
        job("queued"),job("failed"),job("completed"),job("cancelled")});
    snapshot.modelName = "LTX 2.3"; snapshot.deviceName = "Metal";
    check(bool(model.setSnapshot(snapshot)), "snapshot rejected");
    check(model.status.get() == "Generating…", "running status");
    check(model.summary.get() == "3 running · 1 waiting · 1 failed", "queue state counts");
    check(model.model.get() == "LTX 2.3" && model.device.get() == "Metal" &&
          model.execution.get() == "Running", "runtime state");
    snapshot.statusText = "Encoding video…";
    check(bool(model.setSnapshot(snapshot)) && model.status.get() == "Encoding video…", "explicit status lost");
    snapshot.jobs = vm::Value(vm::Array{job("queued")}); snapshot.statusText.clear(); snapshot.modelName.clear();
    check(bool(model.setSnapshot(snapshot)) && model.status.get() == "Queued" && model.execution.get() == "Idle" &&
          model.model.get() == "Unavailable", "queued state");
    HomeQueueSnapshot invalid = snapshot;
    invalid.jobs = vm::Value("not an array");
    check(!model.setSnapshot(invalid) && model.status.get() == "Queued" &&
          model.summary.get() == "0 running · 1 waiting · 0 failed", "invalid input changed displayed state");
    int calls = 0;
    model.setOpenQueueHandler([&] { ++calls; });
    check(model.canOpenQueue.get(), "connected action remains disabled");
    model.openQueue(); check(calls == 1, "open action not delivered");
    model.setOpenQueueHandler({}); model.openQueue();
    check(calls == 1 && !model.canOpenQueue.get(), "detached action still delivered");
    bool rejected = false;
    std::thread other([&] { rejected = !model.setSnapshot(snapshot); }); other.join();
    check(rejected, "wrong-thread snapshot accepted");

    HomeQueueStrings localized;
    localized.ready = "준비";
    localized.counts = [](std::size_t running, std::size_t waiting, std::size_t failed) {
        return std::to_string(running) + "/" + std::to_string(waiting) + "/" + std::to_string(failed);
    };
    HomeQueueModel translated(std::move(localized));
    check(translated.status.get() == "준비" && translated.summary.get() == "0/0/0",
          "host-provided localization was not applied");
}
}
int main() {
    try { exercise(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Native Home queue preserves live generation state and action boundaries\n";
}
