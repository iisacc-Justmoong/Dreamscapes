#include "App/Views/Home/NativeHomePresetSave.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
void exercise() {
    auto composer = HomeComposerModel::create(); int attempts = 0;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        ++attempts;
        if (command.kind != HomeComposerCommandKind::savePreset) throw std::runtime_error("unexpected preset command");
        if (command.value == "unavailable") return ii::core::fail(ii::core::ErrorCode::failed, "Storage unavailable");
        check(command.value == "광고", "preset name changed before delivery"); return {};
    }));
    auto model = HomePresetSaveModel::create(composer, "Save preset", "Name", "Save", "Cancel");
    check(model->enabled.get() && !model->canSave.get(), "empty preset name should be disabled");
    good(model->edit(" \t\n")); check(!model->canSave.get() && !model->save() && attempts == 0, "blank preset name was accepted");
    good(model->edit("\xc2\xa0\xe3\x80\x80")); check(!model->canSave.get(), "Unicode whitespace preset name was accepted");
    check(!model->edit(std::string(1, char(0xff))) && !model->canSave.get(), "invalid UTF-8 changed the preset draft");
    good(model->edit("unavailable")); check(model->canSave.get() && !model->save() && model->draft.get() == "unavailable" && attempts == 1,
                                           "failed save lost its editable draft");
    good(model->edit("광고")); good(model->save());
    check(model->draft.get().empty() && !model->canSave.get() && attempts == 2, "successful preset save did not clear its draft");
    good(composer->setHandler({})); check(!model->enabled.get() && !model->edit("closed"), "detached preset editor remained active");
    model->close(); check(!model->save(), "closed preset editor accepted a save"); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
