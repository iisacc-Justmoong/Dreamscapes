#include "NativePreferencesWorkspace.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace dreamscapes::preferences {
std::vector<CategorySpec> parseCatalog(std::string_view source) {
    std::vector<CategorySpec> result; std::set<std::string> seen;
    while (!source.empty()) {
        auto end=source.find('\n'); auto line=source.substr(0,end);
        auto split=line.find('|');
        if (split!=std::string_view::npos && split && split+1<line.size()) {
            std::string id(line.substr(0,split)),label(line.substr(split+1));
            if (seen.insert(id).second) result.push_back({std::move(id),std::move(label)});
        }
        if (end==std::string_view::npos) break;
        source.remove_prefix(end+1);
    }
    return result;
}
WorkspaceModel::WorkspaceModel(std::vector<CategorySpec> catalog) : catalog_(std::move(catalog)) {
    if (catalog_.empty()) throw std::invalid_argument("Preferences catalog is empty");
}
std::shared_ptr<WorkspaceModel> WorkspaceModel::create(std::vector<CategorySpec> catalog) {
    auto model=std::shared_ptr<WorkspaceModel>(new WorkspaceModel(std::move(catalog))); model->start(); return model;
}
void WorkspaceModel::start() {
    std::vector<std::shared_ptr<Category>> result;
    for (const auto &entry:catalog_) {
        auto row=std::make_shared<Category>(); row->id=entry.id; row->label=entry.label;
        row->owner=shared_from_this(); result.push_back(std::move(row));
    }
    (void)categories.set(std::move(result));
    const auto general=std::ranges::find_if(catalog_,[](const auto &item){return item.id=="General";});
    (void)select(general==catalog_.end()?catalog_.front().id:general->id);
}
ii::core::Result<void> WorkspaceModel::writable() const {
    if (owner_!=std::this_thread::get_id())
        return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Preferences require their owner thread");
    return {};
}
ii::core::Result<void> WorkspaceModel::select(std::string_view id) {
    if (auto valid=writable(); !valid) return valid;
    if (std::ranges::none_of(catalog_,[&](const auto &entry){return entry.id==id;}))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown preferences category");
    if (selecting_) {(void)category.set(std::string(id));selectionPending_=true;return {};}
    selecting_=true;
    try {
        (void)category.set(std::string(id));
        do {
            selectionPending_=false;
            const auto current=category.get();
            for (const auto &entry:categories.get()) {
                (void)entry->selected.set(entry->id==current);
                if (selectionPending_ || category.get()!=current) {selectionPending_=true;break;}
            }
            if (selectionPending_) continue;
            project();
            if (category.get()!=current) selectionPending_=true;
        } while (selectionPending_);
        selecting_=false;return {};
    } catch (...) {selecting_=false;throw;}
}
ii::core::Result<void> WorkspaceModel::setSnapshot(std::string id,std::vector<FieldSpec> values) {
    if (auto valid=writable(); !valid) return valid;
    if (std::ranges::none_of(catalog_,[&](const auto &entry){return entry.id==id;}))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown preferences category");
    std::set<std::string> keys;
    for (const auto &value:values) if (value.id.empty() || !keys.insert(value.id).second)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Preferences fields require unique identifiers");
    snapshots_[id]=std::move(values); ++snapshotRevision_;
    if (id==category.get()) project();
    return {};
}
ii::core::Result<void> WorkspaceModel::clearSnapshot(std::string_view id) {
    if (auto valid=writable(); !valid) return valid;
    if (std::ranges::none_of(catalog_,[&](const auto &entry){return entry.id==id;}))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown preferences category");
    snapshots_.erase(std::string(id)); ++snapshotRevision_;
    if (category.get()==id) project();
    return {};
}
ii::core::Result<void> WorkspaceModel::setChoices(std::string id,std::string key,std::vector<Choice> values,bool available) {
    if (auto valid=writable(); !valid) return valid;
    if (key.empty() || std::ranges::none_of(catalog_,[&](const auto &entry){return entry.id==id;}))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown preferences category or field");
    std::set<std::string> keys;
    for (const auto &value:values) if (value.key.empty() || value.label.empty() || !keys.insert(value.key).second)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Preference choices require unique identifiers and labels");
    auto &state=choices_[{id,key}];
    if (state.entries==values && state.available==available) return {};
    state={std::move(values),available};
    if (id==category.get()) {
        if (projecting_) {projectionPending_=true;return {};}
        const auto snapshot=snapshots_.find(id);
        if (snapshot!=snapshots_.end()) {
            const auto spec=std::ranges::find_if(snapshot->second,[&](const auto &entry){return entry.id==key && entry.choice;});
            if (spec!=snapshot->second.end()) {
                auto next=fields.get();
                for (auto &field:next) if (field->id==key) {field=makeField(id,*spec);break;}
                (void)fields.set(std::move(next));
            }
        }
    }
    return {};
}
bool WorkspaceModel::fieldEditable(const std::string &id,const FieldSpec &spec) const {
    if (!spec.writable || !writeHandler(id)) return false;
    if (!spec.choice) return true;
    const auto state=choices_.find({id,spec.id});
    return state!=choices_.end() && state->second.available;
}
std::shared_ptr<Field> WorkspaceModel::makeField(const std::string &id,const FieldSpec &entry) {
    auto field=std::make_shared<Field>(); field->id=entry.id; field->label=entry.label;
    field->category=id; field->owner=shared_from_this(); field->isChoice=entry.choice;
    field->projectionVersion_=projectionVersion_;
    (void)field->value.set(entry.value); (void)field->editable.set(fieldEditable(id,entry));
    if (entry.choice) {
        const auto state=choices_.find({id,entry.id});
        const bool available=state!=choices_.end() && state->second.available;
        field->choiceKeys.push_back({}); field->options.push_back(available?"No default":"Model catalogue unavailable");
        if (available) for (const auto &choice:state->second.entries) {
            field->choiceKeys.push_back(choice.key); field->options.push_back(choice.label);
        }
        auto current=std::ranges::find(field->choiceKeys,entry.value);
        if (current==field->choiceKeys.end()) {
            field->choiceKeys.push_back(entry.value); field->options.push_back("Unavailable saved model");
            current=field->choiceKeys.end()-1;
        }
        (void)field->index.set(int(current-field->choiceKeys.begin()));
    }
    return field;
}
void WorkspaceModel::project() {
    if (projecting_) {projectionPending_=true;return;}
    projecting_=true;
    try {
        do {
            projectionPending_=false;++projectionVersion_;
            const auto current=category.get();
            const auto found=snapshots_.find(current);
            const bool hasSnapshot=found!=snapshots_.end();
            std::vector<std::shared_ptr<Field>> result;
            if (hasSnapshot) for (const auto &entry:found->second) result.push_back(makeField(current,entry));
            const auto message=categoryStatus_.find(current);
            const auto nextStatus=message!=categoryStatus_.end()?message->second:
                hasSnapshot?std::string{}:"Settings unavailable";
            (void)available.set(hasSnapshot);
            if (projectionPending_ || category.get()!=current) {projectionPending_=true;continue;}
            (void)status.set(nextStatus);
            if (projectionPending_ || category.get()!=current) {projectionPending_=true;continue;}
            (void)fields.set(std::move(result));
            if (category.get()!=current) projectionPending_=true;
        } while (projectionPending_);
        projecting_=false;
    } catch (...) {projecting_=false;throw;}
}
void WorkspaceModel::setWriteHandler(WriteHandler value) {
    if (!writable()) return;
    write_=std::move(value);
    if (projecting_) {projectionPending_=true;return;}
    auto snapshot=snapshots_.find(category.get());
    if (snapshot==snapshots_.end()) return;
    for (const auto &field:fields.get()) {
        auto spec=std::ranges::find_if(snapshot->second,[&](const auto &item){return item.id==field->id;});
        (void)field->editable.set(spec!=snapshot->second.end() && fieldEditable(category.get(),*spec));
    }
}
WorkspaceModel::WriteHandler WorkspaceModel::writeHandler(std::string_view category) const {
    const auto found=categoryWrites_.find(category);
    return found==categoryWrites_.end()?write_:found->second;
}
ii::core::Result<void> WorkspaceModel::setCategoryWriteHandler(std::string id,WriteHandler handler) {
    if (auto valid=writable(); !valid) return valid;
    if (std::ranges::none_of(catalog_,[&](const auto &entry){return entry.id==id;}))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown preferences category");
    if (handler) categoryWrites_[id]=std::move(handler); else categoryWrites_.erase(id);
    if (category.get()==id) {
        if (projecting_) {projectionPending_=true;return {};}
        const auto snapshot=snapshots_.find(id);
        if (snapshot!=snapshots_.end()) for (const auto &field:fields.get()) {
            const auto spec=std::ranges::find_if(snapshot->second,[&](const auto &item){return item.id==field->id;});
            (void)field->editable.set(spec!=snapshot->second.end() && fieldEditable(id,*spec));
        }
    }
    return {};
}
ii::core::Result<void> WorkspaceModel::setCategoryStatus(std::string id,std::string message) {
    if (auto valid=writable(); !valid) return valid;
    if (std::ranges::none_of(catalog_,[&](const auto &entry){return entry.id==id;}))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown preferences category");
    if (message.empty()) categoryStatus_.erase(id); else categoryStatus_[id]=message;
    if (projecting_ && category.get()==id) projectionPending_=true;
    if (category.get()==id) (void)status.set(message.empty() && !snapshots_.contains(id)?"Settings unavailable":message);
    return {};
}
ii::core::Result<void> WorkspaceModel::edit(std::string_view id,std::string_view key,std::string value) {
    if (auto valid=writable(); !valid) return valid;
    auto handler=writeHandler(id);
    if (id!=category.get() || !handler) return ii::core::fail(ii::core::ErrorCode::closed,"Setting is unavailable");
    auto snapshot=snapshots_.find(id);
    if (snapshot==snapshots_.end()) return ii::core::fail(ii::core::ErrorCode::closed,"Setting is unavailable");
    auto spec=std::ranges::find_if(snapshot->second,[&](const auto &entry){return entry.id==key && entry.writable;});
    if (spec==snapshot->second.end()) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Setting is not writable");
    if (spec->choice) {
        const auto state=choices_.find({std::string(id),std::string(key)});
        if (state==choices_.end() || !state->second.available)
            return ii::core::fail(ii::core::ErrorCode::closed,"Preference choices are unavailable");
        if (!value.empty() && value!=spec->value &&
            std::ranges::none_of(state->second.entries,[&](const auto &entry){return entry.key==value;}))
            return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown preference choice");
    }
    // The provider owns validation and persistence; rejected values never become accepted state.
    const auto revision=snapshotRevision_;
    if (auto result=handler(std::string(id),std::string(key),value); !result) {
        (void)status.set(result.error().message); return result;
    }
    // A reentrant provider snapshot is authoritative.
    if (revision!=snapshotRevision_) return {};
    snapshot=snapshots_.find(id);
    if (snapshot!=snapshots_.end()) {
        spec=std::ranges::find_if(snapshot->second,[&](const auto &entry){return entry.id==key;});
        if (spec!=snapshot->second.end()) spec->value=value;
    }
    if (category.get()==id) {
        const auto rows=fields.get();
        for (const auto &field:rows) if (field->id==key) {
            (void)field->value.set(value);
            if (category.get()!=id) break;
            if (field->isChoice) {
                const auto choice=std::ranges::find(field->choiceKeys,value);
                if (choice!=field->choiceKeys.end()) (void)field->index.set(int(choice-field->choiceKeys.begin()));
            }
        }
    }
    if (auto message=categoryStatus_.find(id); message!=categoryStatus_.end()) (void)status.set(message->second);
    else (void)status.set("");
    return {};
}
void WorkspaceModel::sizeChanged(double w,double h,double maxPanel) {
    if (!std::isfinite(w)||!std::isfinite(h)||!std::isfinite(maxPanel)||w<0||h<0||w>8192||h>8192||maxPanel<=0) return;
    const auto side=std::max(1.0,std::min(maxPanel,w/3.0));
    (void)width.set(w); (void)height.set(h); (void)sidebarWidth.set(side); (void)detailWidth.set(std::max(1.0,w-side));
}
void Category::choose() {if (auto model=owner.lock()) (void)model->select(id);}
ii::core::Result<void> WorkspaceModel::requireCurrentField(const Field *field) const {
    if (auto valid=writable(); !valid) return valid;
    if (!field || field->category!=category.get() || field->projectionVersion_!=projectionVersion_ ||
        std::ranges::none_of(fields.get(),[&](const auto &item){return item.get()==field;}))
        return ii::core::fail(ii::core::ErrorCode::closed,"Preference field is no longer active");
    if (!field->editable.get()) return ii::core::fail(ii::core::ErrorCode::closed,"Preference is unavailable");
    return {};
}
ii::core::Result<void> Field::edit(std::string input) {
    if (auto model=owner.lock()) {
        if (auto current=model->requireCurrentField(this); !current) return current;
        return model->edit(category,id,std::move(input));
    }
    return ii::core::fail(ii::core::ErrorCode::closed,"Preferences workspace is closed");
}
ii::core::Result<void> Field::select(int selected) {
    if (!isChoice || selected<0 || std::size_t(selected)>=choiceKeys.size())
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown preference choice");
    const auto model=owner.lock();
    if (!model) return ii::core::fail(ii::core::ErrorCode::closed,"Preferences workspace is closed");
    if (auto current=model->requireCurrentField(this); !current) return current;
    if (choiceKeys[std::size_t(selected)]==value.get()) return {};
    return edit(choiceKeys[std::size_t(selected)]);
}
}
