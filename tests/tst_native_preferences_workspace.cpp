#include "App/Views/Preferences/NativePreferencesWorkspace.h"
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes::preferences;
        auto model=WorkspaceModel::create({{"General","General"},{"Storage","Storage"}});
        check(model->category.get()=="General" && !model->available.get() && model->fields.get().empty(),
              "preferences appeared connected without a provider");
        good(model->select("Storage"));
        check(model->category.get()=="Storage" && !model->available.get(),"category selection failed");
        check(!model->select("unknown") && model->category.get()=="Storage","unknown category was accepted");
        good(model->setSnapshot("Storage",{{"path","Current location","/test",true}}));
        check(model->available.get() && model->fields.get().size()==1 && !model->fields.get()[0]->editable.get(),
              "settings were editable without a write provider");
        std::string accepted;
        model->setWriteHandler([&](std::string category,std::string key,std::string value)->ii::core::Result<void> {
            if (value=="bad") return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Rejected");
            accepted=category+"/"+key+"="+value; return {};
        });
        auto field=model->fields.get()[0];
        check(field->editable.get(),"connected settings are not editable");
        good(field->edit("/new"));
        check(accepted=="Storage/path=/new" && field->value.get()=="/new","edit did not reach the provider");
        check(!field->edit("bad") && field->value.get()=="/new","rejected edit changed accepted value");
        good(model->select("General"));
        check(!model->available.get() && model->fields.get().empty(),"unloaded category showed stale values");
        good(model->select("Storage"));
        check(model->fields.get().size()==1 && model->fields.get()[0]->value.get()=="/new",
              "category navigation lost provider state");
        const auto lastAccepted=accepted;
        check(!field->edit("/retired") && accepted==lastAccepted && model->fields.get()[0]->value.get()=="/new",
              "a retired preference field wrote after category navigation");
        model->setWriteHandler([&](std::string,std::string,std::string)->ii::core::Result<void> {
            return model->setSnapshot("Storage",{{"path","Current location","/provider",true}});
        });
        good(model->fields.get()[0]->edit("/stale"));
        check(model->fields.get()[0]->value.get()=="/provider",
              "reentrant provider snapshot was replaced with stale input");
        auto choices=WorkspaceModel::create({{"Image","Image"}});
        int writes=0;
        choices->setWriteHandler([&](std::string,std::string,std::string)->ii::core::Result<void> {++writes;return {};});
        good(choices->setSnapshot("Image",{{"model","Model","one",true,true}}));
        good(choices->setChoices("Image","model",{{"one","One"},{"two","Two"}},true));
        auto retiredChoice=choices->fields.get()[0];
        good(choices->setChoices("Image","model",{{"two","Two"},{"three","Three"}},true));
        check(!retiredChoice->select(2) && writes==0 && choices->fields.get()[0]->value.get()=="one",
              "a retired preference choice wrote after its catalogue changed");
        auto projection=WorkspaceModel::create({{"General","General"}});
        int projectionWrites=0;
        projection->setWriteHandler([&](std::string,std::string,std::string value)->ii::core::Result<void> {
            if (value=="reject") return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Rejected");
            ++projectionWrites;return {};
        });
        good(projection->setSnapshot("General",{{"name","Name","old",true}}));
        auto oldProjection=projection->fields.get()[0];
        check(!oldProjection->edit("reject"),"fixture did not expose a provider rejection");
        bool attemptedDuringProjection=false,acceptedDuringProjection=false;
        auto projectionObserver=projection->status.observe([&](const std::string &value,std::uint64_t) {
            if (value.empty() && !attemptedDuringProjection) {
                attemptedDuringProjection=true;acceptedDuringProjection=bool(oldProjection->edit("stale"));
            }
        });
        good(projection->setSnapshot("General",{{"name","Name","new",true}}));
        check(attemptedDuringProjection && !acceptedDuringProjection && projectionWrites==0 &&
              projection->fields.get()[0]->value.get()=="new",
              "a preference field wrote during replacement of its projection");
        auto navigation=WorkspaceModel::create({{"General","General"},{"Storage","Storage"}});
        good(navigation->setSnapshot("General",{{"a","A","a"}}));
        good(navigation->setSnapshot("Storage",{{"b","B","b"}}));
        bool redirected=false;
        auto redirect=navigation->category.observe([&](const std::string &value,std::uint64_t) {
            if (value=="Storage" && !redirected) {redirected=true;good(navigation->select("General"));}
        });
        good(navigation->select("Storage"));
        check(redirected && navigation->category.get()=="General" && navigation->categories.get()[0]->selected.get() &&
              !navigation->categories.get()[1]->selected.get() && navigation->fields.get()[0]->id=="a",
              "reentrant preference navigation displayed a stale category");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
