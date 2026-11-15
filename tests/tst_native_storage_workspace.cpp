#include "App/Views/Home/NativeStorageWorkspace.h"
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;
        auto model=storage::WorkspaceModel::create();
        check(!model->available.get() && model->rows.get().empty(),"unconnected storage appeared available");
        vm::Array aliases{
            vm::Value(vm::Object{{"section",vm::Value("files")},{"exists",vm::Value(true)},
                {"key",vm::Value("a")},{"name",vm::Value("Alpha.iisc")},{"relativePath",vm::Value("Folder/Alpha.iisc")}}),
            vm::Value(vm::Object{{"section",vm::Value("files")},{"exists",vm::Value(true)},
                {"key",vm::Value("b")},{"name",vm::Value("Beta.iisc")},{"relativePath",vm::Value("Beta.iisc")}})};
        good(model->setSnapshot(vm::Value(aliases)));
        check(model->available.get() && model->rows.get().size()==2 && model->rows.get()[0]->directory,
              "storage projection did not group folders");
        auto duplicate=aliases; duplicate.push_back(aliases.front());
        check(!model->setSnapshot(vm::Value(duplicate)) && model->rows.get().size()==2,
              "an invalid snapshot replaced the last accepted listing");
        good(model->activate(model->rows.get()[0]->key));
        check(model->directory.get()=="Folder" && model->rows.get().size()==1,"directory navigation failed");
        const auto row=model->rows.get()[0];
        good(model->activate("a"));
        check(model->selectedKey.get()=="a","file selection did not reach the model");
        check(model->rows.get()[0]==row && row->selected.get(),"selection rebuilt an unchanged storage listing");
        good(model->setQuery("missing"));
        check(model->rows.get().empty() && model->selectedKey.get()=="a","filtering discarded selection");
        good(model->setQuery("")); good(model->up());
        check(model->directory.get().empty() && model->selectedKey.get()=="a","navigation lost selection");
        check(!model->canOpen.get(),"open was enabled without an actual file handler");
        bool opened=false;
        model->setOpenHandler([&](vm::Value){},[](const vm::Value &){return false;});
        check(!model->canOpen.get(),"an unavailable provider entry was presented as openable");
        model->setOpenHandler([&](vm::Value entry){opened=vm::text(vm::get(entry,"key"))=="a";});
        check(model->canOpen.get(),"provided open handler was not enabled");
        good(model->openSelected()); check(opened,"open did not send the selected provider entry");
        check(!model->activate("missing"),"unknown item was accepted");
        model->setBrowseHandler([](auto,auto,auto){});
        good(model->refresh());check(model->loading.get(),"a pending storage request was not marked loading");
        good(model->setSnapshot(vm::Value(aliases)));check(!model->loading.get(),"a completed storage request stayed loading");
        good(model->refresh());model->setUnavailable("disconnected");
        check(!model->loading.get() && !model->available.get(),"a failed request stayed loading or available");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
