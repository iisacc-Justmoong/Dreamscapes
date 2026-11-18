#include "NativeCanvasWorkspace.h"
#include <iiJson>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace dreamscapes::canvas {
namespace {
double scale(std::string_view unit,double ppi) {
    if (unit=="px") return 1;
    if (unit=="mm") return ppi/25.4;
    if (unit=="in") return ppi;
    return 0;
}
double convert(double value,std::string_view from,std::string_view to,double ppi) {
    const auto a=scale(from,ppi),b=scale(to,ppi);
    return std::isfinite(value) && std::isfinite(ppi) && ppi>0 && a>0 && b>0 ? value*a/b : 0;
}
vm::Value specification(double w,double h,std::string_view unit,double ppi,std::string_view background) {
    const auto invalid=[](std::string reason){return vm::Value(vm::Object{{"valid",vm::Value(false)},{"error",vm::Value(std::move(reason))}});};
    if (!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0) return invalid("Enter a positive width and height.");
    if (!std::isfinite(ppi)||ppi<=0||ppi>2400) return invalid("Resolution must be between 1 and 2400 PPI.");
    const auto factor=scale(unit,ppi);
    if (factor==0) return invalid("Choose px, mm or in.");
    if (unit=="px" && (std::floor(w)!=w||std::floor(h)!=h)) return invalid("Pixel dimensions must be whole numbers.");
    const auto roundEven=[](double n) {const auto lower=std::floor(n); return n-lower==0.5 && std::fmod(lower,2.0)==0 ? lower : std::floor(n+0.5);};
    const auto pw=roundEven(w*factor),ph=roundEven(h*factor);
    if (!std::isfinite(pw)||!std::isfinite(ph)||pw<1||ph<1||pw>32768||ph>32768||pw*ph>268435456)
        return invalid("Use dimensions up to 32768 px and 268 megapixels.");
    if (background!="White" && background!="Transparent" && background!="Black") return invalid("Choose a canvas background.");
    return vm::Value(vm::Object{{"valid",vm::Value(true)},{"error",vm::Value("")},
        {"width",vm::Value(w)},{"height",vm::Value(h)},{"unit",vm::Value(std::string(unit))},
        {"ppi",vm::Value(ppi)},{"background",vm::Value(std::string(background))},
        {"pixelWidth",vm::Value(static_cast<int>(pw))},{"pixelHeight",vm::Value(static_cast<int>(ph))}});
}
}
WorkspaceModel::WorkspaceModel(vm::Value catalog)
    : catalog_(std::move(catalog)),
      draft_([this](int category,std::string_view query){return sections(category,query);},specification,convert) {}
std::shared_ptr<WorkspaceModel> WorkspaceModel::create(std::string_view input) {
    auto parsed=ii::json::parse(input);
    if (!parsed || vm::array(vm::get(*parsed,"categories")).empty()) throw std::invalid_argument("Canvas catalogue is unavailable");
    auto model=std::shared_ptr<WorkspaceModel>(new WorkspaceModel(std::move(*parsed))); model->start(); return model;
}
void WorkspaceModel::start() {
    std::vector<std::shared_ptr<Category>> result;
    int index=0;
    for (const auto &entry:vm::array(vm::get(catalog_,"categories"))) {
        auto row=std::make_shared<Category>(); row->index=index++; row->id=std::to_string(row->index);
        row->name=vm::text(vm::get(entry,"name")); row->owner=shared_from_this(); result.push_back(std::move(row));
    }
    (void)categories.set(std::move(result));
    (void)send(vm::CanvasDraftAction::begin);
}
ii::core::Result<void> WorkspaceModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Canvas workspace requires its owner thread");
    return {};
}
vm::Value WorkspaceModel::sections(int category,std::string_view input) const {
    auto query=vm::lower(vm::trim(std::string(input)));
    for (std::size_t at=0;at<query.size();++at) {
        const bool multiply=query.compare(at,2,"×")==0;
        if (!multiply && query[at]!='x') continue;
        auto left=at,right=at+(multiply?2:1);
        while (left>0 && std::isspace(static_cast<unsigned char>(query[left-1]))) --left;
        while (right<query.size() && std::isspace(static_cast<unsigned char>(query[right]))) ++right;
        if (left>0 && right<query.size() && std::isdigit(static_cast<unsigned char>(query[left-1])) &&
            std::isdigit(static_cast<unsigned char>(query[right]))) query.replace(at,multiply?2:1," ");
    }
    std::istringstream stream(query); std::vector<std::string> words;
    for (std::string word;stream>>word;) words.push_back(std::move(word));
    vm::Array groups;
    int index=0;
    for (const auto &cat:vm::array(vm::get(catalog_,"categories"))) {
        const auto categoryName=vm::text(vm::get(cat,"name"));
        for (const auto &section:vm::array(vm::get(cat,"sections"))) {
            const auto sectionName=vm::text(vm::get(section,"name")); vm::Array entries;
            for (const auto &item:vm::array(vm::get(section,"presets"))) {
                if (words.empty() && index!=category) continue;
                const auto haystack=vm::lower(vm::text(vm::get(item,"name"))+" "+categoryName+" "+sectionName+" "+
                    std::to_string(vm::number(vm::get(item,"width")))+" "+std::to_string(vm::number(vm::get(item,"height")))+" "+
                    vm::text(vm::get(item,"unit"))+" "+std::to_string(vm::number(vm::get(item,"pixelWidth")))+" "+
                    std::to_string(vm::number(vm::get(item,"pixelHeight"))));
                if (std::ranges::any_of(words,[&](const auto &word){return haystack.find(word)==std::string::npos;})) continue;
                auto value=vm::object(item); value["category"]=vm::Value(categoryName); value["categoryIndex"]=vm::Value(index);
                value["section"]=vm::Value(sectionName); entries.emplace_back(std::move(value));
            }
            if (!entries.empty()) groups.emplace_back(vm::Object{{"name",vm::Value(words.empty()?sectionName:categoryName+" · "+sectionName)},
                                                                  {"presets",vm::Value(std::move(entries))}});
        }
        ++index;
    }
    return vm::Value(std::move(groups));
}
void WorkspaceModel::synchronize(const vm::Value &state) {
    const auto category=static_cast<int>(vm::number(vm::get(state,"categoryIndex")));
    for (const auto &entry:categories.get()) (void)entry->selected.set(entry->index==category);
    (void)widthText.set(vm::text(vm::get(state,"draftWidth"))); (void)heightText.set(vm::text(vm::get(state,"draftHeight")));
    (void)ppiText.set(vm::text(vm::get(state,"resolution"))); (void)query.set(vm::text(vm::get(state,"query")));
    (void)unit.set(vm::text(vm::get(state,"unit"))); (void)background.set(vm::text(vm::get(state,"canvasBackground")));
    (void)selectionName.set(vm::text(vm::get(state,"selectionName")));
    auto spec=vm::get(state,"specification");
    (void)valid.set(vm::boolean(vm::get(spec,"valid"))); (void)canCreate.set(valid.get() && bool(create_));
    (void)status.set(vm::text(vm::get(spec,"error")));
    const auto sections=vm::get(state,"sections");
    if (projectedSections_ && *projectedSections_==sections) return;
    projectedSections_=sections;
    std::vector<std::shared_ptr<Preset>> rows;
    for (const auto &group:vm::array(sections))
        for (const auto &entry:vm::array(vm::get(group,"presets"))) {
            auto row=std::make_shared<Preset>(); row->id=vm::text(vm::get(entry,"id"));
            row->name=vm::text(vm::get(entry,"name")); row->value=entry; row->owner=shared_from_this(); rows.push_back(std::move(row));
        }
    (void)presets.set(std::move(rows));
}
ii::core::Result<void> WorkspaceModel::send(vm::CanvasDraftAction action,std::string key,vm::Value value) {
    if (auto result=writable(); !result) return result;
    auto response=draft_.commands().send({action,std::move(key),std::move(value)});
    if (!response) return ii::core::fail(ii::core::ErrorCode::failed,"Canvas draft command failed",response.error().cause);
    synchronize(response->presentation);
    if (action==vm::CanvasDraftAction::create) {
        if (!response->created || !create_) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Canvas cannot be created");
        auto handler=create_; auto result=handler(std::move(*response->created));
        if (!result) (void)status.set(result.error().message);
        return result;
    }
    return {};
}
ii::core::Result<void> WorkspaceModel::chooseCategory(int value) {
    if (value<0 || static_cast<std::size_t>(value)>=vm::array(vm::get(catalog_,"categories")).size())
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown canvas category");
    return send(vm::CanvasDraftAction::chooseCategory,{},vm::Value(value));
}
ii::core::Result<void> WorkspaceModel::choosePreset(vm::Value value) {
    if (auto result=writable(); !result) return result;
    auto id=vm::text(vm::get(value,"id"));
    const auto found=std::ranges::find_if(presets.get(),[&](const auto &entry){return entry->id==id;});
    if (found==presets.get().end())
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown canvas preset");
    auto selected=(*found)->value;
    return send(vm::CanvasDraftAction::choose,{},std::move(selected));
}
ii::core::Result<void> WorkspaceModel::search(std::string value) {return send(vm::CanvasDraftAction::search,{},vm::Value(std::move(value)));}
ii::core::Result<void> WorkspaceModel::customSize() {return send(vm::CanvasDraftAction::customSize);}
ii::core::Result<void> WorkspaceModel::edit(std::string_view key,std::string value) {
    if (key!="draftWidth" && key!="draftHeight" && key!="resolution" && key!="canvasBackground")
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown canvas field");
    return send(vm::CanvasDraftAction::write,std::string(key),vm::Value(std::move(value)));
}
ii::core::Result<void> WorkspaceModel::changeUnit(std::string value) {
    if (value!="px" && value!="mm" && value!="in") return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown canvas unit");
    return send(vm::CanvasDraftAction::changeUnit,{},vm::Value(std::move(value)));
}
ii::core::Result<void> WorkspaceModel::createCanvas() {return send(vm::CanvasDraftAction::create);}
void WorkspaceModel::setCreateHandler(CreateHandler handler) {
    if (!writable()) return;
    create_=std::move(handler); (void)canCreate.set(valid.get() && bool(create_));
}
void WorkspaceModel::sizeChanged(double w,double h,double maxPanel) {
    if (!std::isfinite(w)||!std::isfinite(h)||!std::isfinite(maxPanel)||w<0||h<0||w>8192||h>8192||maxPanel<=0) return;
    const auto panel=std::max(1.0,std::min(maxPanel,w/3.0));
    (void)width.set(w); (void)height.set(h); (void)detailWidth.set(panel); (void)listWidth.set(std::max(1.0,w-panel));
}
void Category::choose() {if (auto model=owner.lock()) (void)model->chooseCategory(index);}
void Preset::choose() {if (auto model=owner.lock()) (void)model->choosePreset(value);}
}
