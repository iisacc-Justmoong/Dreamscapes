#include "App/Views/Result/NativeReviewGrid.h"
#include "App/Views/Result/NativeReviewGridImages.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef DREAMSCAPES_REVIEW_GRID_WIDGET_TEST
#include "App/Views/Result/NativeReviewGridView.h"
#include "App/Native/NativePreviewStatus.h"
#include <iiGUI/WidgetState.h>
#endif

namespace {
using namespace dreamscapes;
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
vm::Value entry(int id) {return vm::Value(vm::Object{{"id",vm::Value(std::to_string(id))},
    {"documentSource",vm::Value("file:///result/"+std::to_string(id)+".iisc")},
    {"thumbnailSource",vm::Value("file:///result/"+std::to_string(id)+".png")},{"mediaType",vm::Value("Image")}});}
void exercise() {
    auto session=std::make_shared<review::Session>(); auto gallery=review::ReviewGalleryModel::create(session);
    bool invalidWidth=false;
    try {(void)review::ReviewGridModel::create(gallery,1e-300);} catch (const std::invalid_argument &) {invalidWidth=true;}
    check(invalidWidth,"grid accepted an unbounded column count");
    session->replaceEntries({entry(1),entry(2),entry(3)}); session->selectAt(0); session->selectAt(1,false,true);
    auto grid=review::ReviewGridModel::create(gallery,160);
    check(bool(grid->setViewport(800,500)) && grid->columns.get()==5 && grid->cellSize.get()==160,
          "grid did not use available width for five square columns");
    check(bool(grid->setViewport(500,500)) && grid->columns.get()==3 && std::abs(grid->cellSize.get()-500.0/3)<0.001,
          "grid did not recompute its responsive columns");
    check(bool(grid->setViewport(100,500)) && grid->columns.get()==2 && grid->cellSize.get()==50,
          "a narrow grid did not retain the minimum two columns");
    check(session->selectedCount()==2 && session->focusedEntry()==entry(2),"resizing changed the review state");
    auto reentrant=grid->cellSize.observe([&](double value,std::uint64_t) {
        if (value==175) check(bool(grid->setViewport(600,400)),"nested resize was rejected");
    });
    check(!grid->setViewport(700,500) && grid->columns.get()==3 && grid->cellSize.get()==200 &&
          grid->viewportWidth.get()==600 && grid->viewportHeight.get()==400,
          "an obsolete layout overwrote a newer resize");
    check(!grid->setViewport(-1,500) && !grid->setViewport(INFINITY,500),"grid accepted an invalid viewport");
    bool wrongThread=false; std::thread worker([&]{wrongThread=!grid->setViewport(200,200).has_value();}); worker.join();
    check(wrongThread,"grid geometry changed on a non-owner thread");
    grid->close(); check(!grid->active() && !grid->setViewport(200,200),"a retired grid accepted geometry changes");
    check(session->selectedCount()==2,"retiring the grid modified the product session"); gallery->close();
}
void imageLifetime() {
    struct Resolver final : lvui::native::ImageResolver<ii::gui::RasterImage> {
        explicit Resolver(std::uint8_t red) : pixels(*ii::gui::rgba_image(1,1,{red,0,0,255})) {}
        std::shared_ptr<const ii::gui::RasterImage> image(std::string_view,std::uint32_t,std::uint32_t) const override {return pixels;}
        std::shared_ptr<const ii::gui::RasterImage> pixels;
    };
    auto session=std::make_shared<review::Session>(); session->replaceEntries({entry(0)});
    auto gallery=review::ReviewGalleryModel::create(session); auto first=std::make_shared<Resolver>(1),second=std::make_shared<Resolver>(2);
    const auto token=gallery->items.get()[0]->token;
    auto binding=review::ReviewGridImages::create(gallery,first);
    check(gallery->imageForToken(token,1,1)==first->pixels,"grid image binding did not install its resolver");
    gallery->setImageResolver(second); binding->close();
    check(gallery->imageForToken(token,1,1)==second->pixels,"an old image binding removed a replacement resolver");
    binding=review::ReviewGridImages::create(gallery,first); binding.reset();
    check(!gallery->imageForToken(token,1,1),"retiring the grid retained its worker-backed resolver");
    auto retired=std::make_shared<Resolver>(3);
    std::weak_ptr<Resolver> retiredWeak=retired;
    binding=review::ReviewGridImages::create(gallery,retired);
    retired.reset();
    gallery->close();
    binding.reset();
    check(retiredWeak.expired(),"a closed gallery retained its worker-backed resolver");
}
#ifdef DREAMSCAPES_REVIEW_GRID_WIDGET_TEST
template<class T> T take(ii::core::Result<T> value) {
    if (!value) {if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message);}
    return std::move(*value);
}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
class Preview final : public lvui::native::ImageResolver<ii::gui::RasterImage>,public native::PreviewStatusSource {
  public:
    ii::core::Observable<native::PreviewStatus> status{native::PreviewStatus::loading};
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view,std::uint32_t,std::uint32_t) const override {
        return status.get()==native::PreviewStatus::ready ? *ii::gui::rgba_image(1,1,{10,20,30,255}) : nullptr;
    }
    native::PreviewStatus previewStatus(std::string_view,std::uint32_t,std::uint32_t) const override {return status.get();}
};
void widgets() {
    auto session=std::make_shared<review::Session>(); auto gallery=review::ReviewGalleryModel::create(session);
    std::vector<vm::Value> entries; for (int i=0;i<1000;++i) entries.push_back(entry(i));
    session->replaceEntries(entries); auto grid=review::ReviewGridModel::create(gallery,160);
    auto preview=std::make_shared<Preview>(); gallery->setImageResolver(preview);
    std::vector<vm::Value> detail,edited,compared;
    gallery->setDetailHandler([&](auto values){detail=std::move(values);});
    gallery->setEditHandler([&](auto values){edited=std::move(values);});
    gallery->setCompareHandler([&](auto values){compared=std::move(values);});
    auto state=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();});
    auto root=take(state->content_host({},"root",{640,420}));
    good(root.set_content([&](ii::gui::WidgetBuilder &builder){return review::buildReviewGrid(builder,{},grid);}));
    (void)take(state->drawing_snapshot({640,420}));
    (void)take(state->drawing_snapshot({640,420}));
    (void)take(state->snapshots());
    auto list=take(root.content_item("gridResults"));
    auto child=[&](std::size_t i,std::string_view id) {
        auto value=list.keyed_content_item(gallery->items.get().at(i)->token,id);
        if (!value) {
            const auto keys=take(list.keyed_content_keys());
            throw std::runtime_error("Grid item "+std::to_string(i)+" / "+std::string(id)+": "+value.error().message+
                                     " token="+gallery->items.get().at(i)->token+" first="+(keys.empty()?"empty":keys.front()));
        }
        return *value;
    };
    auto row=[&](std::size_t i){return child(i,"gridTile");};
    check(take(child(0,"gridTitle").snapshot()).text=="01 · Image · 0",
          "grid title did not include the original result identity");
    check(take(child(0,"gridPreviewStatus").snapshot()).text=="Loading…",
          "grid omitted the loading state");
    (void)preview->status.set(native::PreviewStatus::failed);
    check(take(child(0,"gridPreviewStatus").snapshot()).text=="Preview unavailable",
          "grid omitted a failed preview state");
    (void)preview->status.set(native::PreviewStatus::ready);
    check(!take(child(0,"gridPreviewStatus").snapshot()).visible,
          "grid retained its loading state after the image arrived");
    const auto nodeCount=take(state->snapshots()).size();
    if (grid->columns.get()!=4 || nodeCount>=400)
        throw std::runtime_error("grid did not virtualize its responsive rows: columns="+std::to_string(grid->columns.get())+
                                 " nodes="+std::to_string(nodeCount)+" width="+std::to_string(grid->viewportWidth.get()));
    for (int i=1000;i<2000;++i) entries.push_back(entry(i));
    session->replaceEntries(entries);
    check(take(state->snapshots()).size()<=nodeCount+20,"offscreen results created unbounded widgets");
    auto click=[&](std::size_t i,bool shift=false,std::uint32_t count=1) {
        const auto box=take(row(i).snapshot()).bounds;
        good(state->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=box.x+5,.y=box.y+5,.shift=shift,.buttons=1,.native_click_count=count}));
        good(state->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=box.x+5,.y=box.y+5,.shift=shift,.buttons=1,.native_click_count=count}));
    };
    click(0); click(2,true);
    check(session->selectedCount()==3,"grid lost range selection");
    const auto box=take(row(2).snapshot()).bounds;
    good(state->input({.kind=ii::gui::WidgetInputKind::context_request,.x=box.x+5,.y=box.y+5}));
    good(child(2,"gridContextCompare").activate());
    check(compared==std::vector<vm::Value>{entries[0],entries[1],entries[2]},"grid context changed its original compare targets");
    good(list.focus()); good(state->input({.kind=ii::gui::WidgetInputKind::key_down,.key=ii::gui::WidgetKey::enter}));
    check(edited==compared,"grid Enter did not edit its selection");
    click(1,false,1); click(1,false,2);
    check(detail==std::vector<vm::Value>{entries[1]} && session->selectedCount()==3,"grid double-click changed selection or detail target");
    auto scroll=take(root.content_item("gridResults/viewport")); good(scroll.set_scroll_offset(800));
    const auto offset=take(scroll.snapshot()).scroll_offset;
    entries.push_back(entry(2000)); session->replaceEntries(entries);
    check(offset>0 && take(scroll.snapshot()).scroll_offset==offset && session->selectedCount()==3,
          "a new result changed the scroll position or selection");
    good(root.set_extent({480,420}));
    (void)take(state->drawing_snapshot({480,420})); (void)take(state->drawing_snapshot({480,420}));
    (void)take(state->snapshots());
    check(grid->columns.get()==3 && session->selectedCount()==3,"grid resize lost its layout or selection");
    state->close(); grid->close(); gallery->close();
}
#endif
}
int main() {try {exercise(); imageLifetime();
#ifdef DREAMSCAPES_REVIEW_GRID_WIDGET_TEST
    widgets();
#endif
} catch (const std::exception &error) {std::cerr<<error.what()<<'\n'; return 1;}}
