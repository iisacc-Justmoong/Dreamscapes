#include "NativeStorageWorkspace.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace dreamscapes::storage {
std::shared_ptr<WorkspaceModel> WorkspaceModel::create() {return std::shared_ptr<WorkspaceModel>(new WorkspaceModel);}
ii::core::Result<void> WorkspaceModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Storage workspace requires its owner thread");
    return {};
}
ii::core::Result<void> WorkspaceModel::setSnapshot(vm::Value aliases) {
    if (auto valid=writable(); !valid) return valid;
    if (!std::holds_alternative<vm::Array>(aliases.storage()))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Storage snapshot must be a list");
    std::set<std::string> keys;
    for (const auto &entry:vm::array(aliases)) {
        auto key=vm::text(vm::get(entry,"key"));
        if (key.empty() || !keys.insert(key).second)
            return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Storage entry keys must be unique and non-empty");
    }
    snapshot_.aliases=std::move(aliases);
    if (!selectedKey.get().empty() && !keys.contains(selectedKey.get())) (void)selectedKey.set("");
    (void)loading.set(false); (void)available.set(true); project(); return {};
}
ii::core::Result<void> WorkspaceModel::setSection(std::string section) {
    if (auto valid=writable(); !valid) return valid;
    if (section!="files" && section!="asset-library" && section!="generation-history")
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown storage section");
    if (section==snapshot_.section) return {};
    snapshot_.section=std::move(section); snapshot_.directory.clear(); snapshot_.query.clear();
    (void)directory.set(""); (void)query.set(""); (void)selectedKey.set("");
    (void)heading.set(snapshot_.section=="asset-library"?"Assets":snapshot_.section=="generation-history"?"Generation History":"Files");
    return refresh();
}
ii::core::Result<void> WorkspaceModel::setQuery(std::string value) {
    if (auto valid=writable(); !valid) return valid;
    if (snapshot_.query==value) return {};
    snapshot_.query=value; (void)query.set(std::move(value)); return refresh();
}
ii::core::Result<void> WorkspaceModel::activate(std::string_view key) {
    if (auto valid=writable(); !valid) return valid;
    const auto found=std::ranges::find_if(rows.get(),[&](const auto &row){return row->key==key;});
    if (found==rows.get().end()) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown storage entry");
    auto row=*found;
    if (row->directory) {
        snapshot_.directory=row->path; (void)directory.set(row->path); return refresh();
    } else {
        (void)selectedKey.set(row->key);
        for (const auto &entry:rows.get()) (void)entry->selected.set(entry->key==row->key);
        refreshOpen();
    }
    return {};
}
ii::core::Result<void> WorkspaceModel::up() {
    if (auto valid=writable(); !valid) return valid;
    const auto slash=snapshot_.directory.rfind('/');
    snapshot_.directory=slash==std::string::npos?std::string{}:snapshot_.directory.substr(0,slash);
    (void)directory.set(snapshot_.directory); return refresh();
}
void WorkspaceModel::setBrowseHandler(BrowseHandler handler) {
    if (!writable()) return;
    browse_=std::move(handler);
}
ii::core::Result<void> WorkspaceModel::refresh() {
    if (auto valid=writable(); !valid) return valid;
    if (!browse_) {project(); return {};}
    auto handler=browse_;
    (void)loading.set(true); (void)available.set(false); (void)canOpen.set(false); (void)canUp.set(!snapshot_.directory.empty());
    (void)rows.set({}); (void)status.set("Reading Society storage…");
    try {handler(snapshot_.section,snapshot_.directory,snapshot_.query);}
    catch (...) {setUnavailable("Cannot request Society storage");
        return ii::core::fail(ii::core::ErrorCode::failed,"Cannot request Society storage",std::current_exception());}
    return {};
}
void WorkspaceModel::setUnavailable(std::string message) {
    if (!writable()) return;
    snapshot_.aliases=vm::Value(vm::Array{});
    (void)loading.set(false); (void)available.set(false); (void)canOpen.set(false); (void)rows.set({});
    (void)status.set(std::move(message));
}
void WorkspaceModel::refreshOpen() {
    const auto key=selectedKey.get();
    const auto &entries=vm::array(snapshot_.aliases);
    const auto found=std::ranges::find_if(entries,[&](const auto &entry){
        return vm::text(vm::get(entry,"key"))==key && vm::text(vm::get(entry,"section"))==snapshot_.section
            && vm::boolean(vm::get(entry,"exists"));
    });
    bool eligible=found!=entries.end();
    if (eligible && canOpenEntry_) try {eligible=canOpenEntry_(*found);} catch (...) {eligible=false;}
    (void)canOpen.set(available.get() && !key.empty() && eligible && bool(open_));
}
void WorkspaceModel::project() {
    snapshot_.selectedKey=selectedKey.get();
    std::vector<std::shared_ptr<Row>> result;
    if (available.get()) {
        auto projected=projector_.queries().send(snapshot_);
        if (projected) for (const auto &entry:vm::array(projected->entries)) {
            auto row=std::make_shared<Row>();
            row->key=vm::text(vm::get(entry,"key")); row->name=vm::text(vm::get(entry,"name"));
            row->path=vm::text(vm::get(entry,"relativePath")); row->directory=vm::boolean(vm::get(entry,"isDirectory"));
            row->owner=shared_from_this(); (void)row->selected.set(row->key==selectedKey.get()); result.push_back(std::move(row));
        }
        (void)status.set(projected ? std::to_string(result.size())+" items" : "Storage projection unavailable");
    }
    (void)canUp.set(!snapshot_.directory.empty()); (void)rows.set(std::move(result)); refreshOpen();
}
void WorkspaceModel::setOpenHandler(std::function<void(vm::Value)> handler,std::function<bool(const vm::Value &)> canOpen) {
    if (!writable()) return;
    open_=std::move(handler); canOpenEntry_=std::move(canOpen); refreshOpen();
}
ii::core::Result<void> WorkspaceModel::openSelected() {
    if (auto valid=writable(); !valid) return valid;
    if (!canOpen.get()) return ii::core::fail(ii::core::ErrorCode::closed,"Selected file cannot be opened");
    const auto key=selectedKey.get();
    const auto found=std::ranges::find_if(vm::array(snapshot_.aliases),[&](const auto &entry){return vm::text(vm::get(entry,"key"))==key;});
    auto handler=open_; auto entry=*found; handler(std::move(entry)); return {};
}
void WorkspaceModel::sizeChanged(double w,double h) {
    if (!std::isfinite(w) || !std::isfinite(h) || w<0 || h<0 || w>8192 || h>8192) return;
    (void)width.set(w); (void)height.set(h);
}
void Row::activate() {if (auto model=owner.lock()) (void)model->activate(key);}
}
