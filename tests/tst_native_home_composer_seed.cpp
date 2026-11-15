#include "App/Views/Home/NativeHomeComposerSeed.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
void exercise() {
    auto composer = HomeComposerModel::create();
    HomeComposerSnapshot snapshot; snapshot.seed = -1; snapshot.seedLabel = "Random";
    good(composer->setSnapshot(snapshot));
    int commands = 0;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        ++commands;
        auto next = composer->snapshot.get();
        if (command.kind == HomeComposerCommandKind::applySeed) {
            if (command.value == "invalid") return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid seed");
            next.seed = 123; next.seedLabel = command.value;
        } else if (command.kind == HomeComposerCommandKind::randomSeed) {
            next.seed = -1; next.seedLabel = "Random";
        } else throw std::runtime_error("unexpected seed command");
        return composer->setSnapshot(std::move(next));
    }));
    auto seed = HomeComposerSeedModel::create(composer, "Seed", "Random", "Apply");
    check(seed->value.get() == "Random" && seed->enabled.get(), "seed state was not projected");
    good(seed->begin()); check(seed->draft.get().empty() && commands == 0, "random seed did not open an empty draft");
    good(seed->edit("invalid")); check(!seed->apply() && seed->draft.get() == "invalid" && commands == 1,
                                     "invalid seed was committed or its draft was lost");
    good(seed->edit("123"));
    auto next = composer->snapshot.get(); next.seed = 456; next.seedLabel = "456"; good(composer->setSnapshot(next));
    check(seed->draft.get() == "123" && seed->value.get() == "456", "external update overwrote an open seed draft");
    good(seed->apply()); check(composer->snapshot.get().seed == 123 && seed->value.get() == "123" && commands == 2,
                               "seed apply did not use the existing command");
    good(seed->begin()); check(seed->draft.get() == "123", "opening did not reflect the current seed");
    good(seed->randomize()); check(composer->snapshot.get().seed == -1 && commands == 3, "random seed command was lost");
    good(seed->setViewport(140, 40)); check(seed->viewportWidth.get() == 140, "seed setting geometry was lost");
    check(!seed->setViewport(std::numeric_limits<double>::quiet_NaN(), 40), "invalid seed geometry was accepted");
    good(composer->setHandler({})); check(!seed->enabled.get() && !seed->begin(), "detached seed editor remained active");
    seed->close(); check(!seed->edit("9") && commands == 3, "closed seed editor accepted input");
    composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
