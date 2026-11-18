#include "App/Views/Result/NativeReviewInspectorViewModel.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef DREAMSCAPES_REVIEW_INSPECTOR_WIDGET_TEST
#include "App/Views/Result/NativeReviewInspectorView.h"
#include "iiGUI/WidgetState.h"
#include <iiGUI>
#endif

using namespace dreamscapes;
namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
review::InspectorViewStrings labels() {
    return {"INSPECTOR", "Focused result", "Preview", "Expand", "Save current", "Unavailable", "No result selected",
        [](std::size_t count) { return std::to_string(count) + " selected"; },
        [](std::string_view label) { return std::string(label); },
        [](std::string_view value) { return "Job · " + std::string(value); },
        [](std::string_view value) { return "Original · " + std::string(value); }};
}
review::InspectorDisplayField::Ptr row(const std::shared_ptr<review::InspectorViewModel> &view, std::string_view key) {
    const auto values = view->fields.get();
    const auto found = std::ranges::find_if(values, [&](const auto &value) { return value->key == key; });
    if (found == values.end()) throw std::runtime_error("Inspector field missing");
    return *found;
}
void exercise() {
    auto session = std::make_shared<review::Session>();
    auto inspector = review::ReviewInspectorModel::create(session);
    bool invalidLayout=false;
    try { (void)review::InspectorViewModel::create(inspector,labels(),{178,22,300,10}); }
    catch (const std::invalid_argument &) { invalidLayout=true; }
    check(invalidLayout,"a long-field viewport shorter than the minimum was accepted");
    auto view = review::InspectorViewModel::create(inspector, labels(), {178,22,300,200});
    check(view->fields.get().empty() && view->selectionSummary.get()=="Focused result" &&
          view->originalText.get()=="Original · Unavailable" && !view->canExpand.get(),
          "empty inspector displayed result data or enabled actions");
    check(view->setViewport(320,706).has_value() && view->viewportWidth.get()==320 &&
          view->viewportHeight.get()==706 && view->previewHeight.get()==178, "inspector geometry mismatch");
    check(!view->setViewport(-1,100).has_value() && view->viewportWidth.get()==320,
          "invalid geometry changed the inspector");
    const vm::Value first(vm::Object{{"id",vm::Value("a")},{"imageSource",vm::Value("file:///a.png")},
        {"documentSource",vm::Value("file:///a.iisc")},{"seed",vm::Value("17")},{"prompt",vm::Value("first")}});
    const vm::Value second(vm::Object{{"id",vm::Value("b")},{"imageSource",vm::Value("file:///b.png")},
        {"seed",vm::Value("29")},{"prompt",vm::Value(std::string(100000,'p'))}});
    session->replaceEntries({first,second}); session->focusAt(0);
    auto prompt = row(view,"Prompt"); auto seed = row(view,"Seed");
    check(prompt->text.get()=="first" && prompt->usesWrappedText.get() && seed->text.get()=="17" &&
          view->previewSource.get()=="file:///a.png" && view->jobText.get()=="Job · a" &&
          view->originalText.get()=="Original · file:///a.iisc", "focused result was not faithfully projected");
    check(prompt->setMeasuredHeight(80).has_value() && prompt->height.get()==80 &&
          prompt->setMeasuredHeight(1000).has_value() && prompt->height.get()==300,
          "measured field height ignored its viewport bounds");
    vm::Value expanded,saved;
    inspector->setExpandHandler([&](vm::Value value){expanded=std::move(value);});
    inspector->setSaveHandler([&](vm::Value value){saved=std::move(value);});
    view->expand(); view->save();
    check(view->canExpand.get() && view->canSave.get() && expanded==first && saved==first,
          "preview actions lost the focused original value");
    session->selectAt(0); session->selectAt(1,false,true);
    check(view->selectionSummary.get()=="2 selected" && row(view,"Seed")==seed &&
          seed->text.get().find("17")!=std::string::npos && seed->text.get().find("29")!=std::string::npos,
          "mixed selection lost its values or stable field identity");
    session->clearSelection(); session->focusAt(1);
    prompt=row(view,"Prompt");
    check(!prompt->usesWrappedText.get() && prompt->height.get()==200,
          "long inspector text was not routed to the document-backed reader");
    const auto snap=prompt->document->snapshot();
    check(snap && (*snap)->byte_size()==100000 && (*snap)->read({0,(*snap)->length()},200000).value()==std::string(100000,'p'),
          "long inspector text was truncated");
    bool wrongThread=false;
    std::thread other([&] { wrongThread=!view->setViewport(100,100).has_value(); }); other.join();
    check(wrongThread, "inspector accepted layout from a different thread");
    view->close(); session->focusAt(0);
    check(!view->active() && !view->canExpand.get() && !view->canSave.get(), "closed inspector retained actions");

    auto changingSession=std::make_shared<review::Session>();
    auto changingInspector=review::ReviewInspectorModel::create(changingSession);
    auto changingView=review::InspectorViewModel::create(changingInspector,labels(),{178,22,300,200});
    changingSession->replaceEntries({first}); changingSession->focusAt(0);
    auto changingPrompt=row(changingView,"Prompt");
    auto closeOnUpdate=changingPrompt->text.observe([&](const auto &,std::uint64_t){changingView->close();});
    const vm::Value updated(vm::Object{{"id",vm::Value("a")},{"imageSource",vm::Value("file:///a.png")},
        {"documentSource",vm::Value("file:///a.iisc")},{"seed",vm::Value("17")},{"prompt",vm::Value("changed")}});
    changingSession->replaceEntries({updated});
    check(!changingView->active(), "an inspector closed during notification was reopened");

    auto resizingSession=std::make_shared<review::Session>();
    auto resizingInspector=review::ReviewInspectorModel::create(resizingSession);
    auto resizingView=review::InspectorViewModel::create(resizingInspector,labels(),{178,22,300,200});
    auto closeOnResize=resizingView->viewportWidth.observe([&](double,std::uint64_t){resizingView->close();});
    check(!resizingView->setViewport(400,800).has_value() && resizingView->viewportHeight.get()==1,
          "an inspector retired during layout continued publishing geometry");
}
#ifdef DREAMSCAPES_REVIEW_INSPECTOR_WIDGET_TEST
template<class T> T take(ii::core::Result<T> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
    return std::move(*value);
}
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view id) {
    for (const auto &node : take(state->snapshots()))
        if (node.id==id || node.id.ends_with(std::string("/")+std::string(id))) return take(state->find(node.id));
    throw std::runtime_error("Inspector widget not found: "+std::string(id));
}
class Images final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    mutable std::vector<std::string> requests;
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view address,std::uint32_t,std::uint32_t) const override {
        requests.emplace_back(address);
        return take(ii::gui::rgba_image(1,1,{255,0,0,255}));
    }
};
void widgets() {
    auto session=std::make_shared<review::Session>();
    auto inspector=review::ReviewInspectorModel::create(session);
    auto view=review::InspectorViewModel::create(inspector,labels(),{178,22,300,200});
    vm::Value expanded,saved;
    inspector->setExpandHandler([&](vm::Value value){expanded=std::move(value);});
    inspector->setSaveHandler([&](vm::Value value){saved=std::move(value);});
    auto state=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();});
    auto images=std::make_shared<Images>();
    auto root=take(state->content_host({},"root",{320,706}));
    good(root.set_content([&](ii::gui::WidgetBuilder &builder){return review::buildReviewInspector(builder,{},view,images);}));
    (void)take(state->drawing_snapshot({320,706}));
    check(!take(find(state,"inspectorExpand").snapshot()).enabled,
          "empty inspector enabled expand");
    const vm::Value first(vm::Object{{"id",vm::Value("a")},{"imageSource",vm::Value("file:///a.png")},
        {"documentSource",vm::Value("file:///a.iisc")},{"seed",vm::Value("123456789")},{"prompt",vm::Value("first prompt")}});
    session->replaceEntries({first}); session->focusAt(0);
    (void)take(state->drawing_snapshot({320,706}));
    check(std::ranges::find(images->requests,"file:///a.png")!=images->requests.end(),
          "focused preview was not sent to the injected image resolver");
    good(find(state,"inspectorExpand").activate()); good(find(state,"inspectorSave").activate());
    check(expanded==first && saved==first && take(find(state,"inspectorJob").snapshot()).text=="Job · a",
          "native inspector actions lost their focused result");
    ii::gui::Widget editor;
    const auto nodes=take(state->snapshots());
    std::string seedScope;
    for (const auto &node : nodes)
        if (node.id.ends_with("/inspectorFieldLabel") && node.text=="Seed")
            seedScope=node.id.substr(0,node.id.size()-std::string("/inspectorFieldLabel").size());
    for (const auto &node : nodes)
        if (!seedScope.empty() && node.id.starts_with(seedScope+"/") && node.id.ends_with("/inspectorFieldText"))
            editor=take(state->find(node.id));
    check(!editor.is_null(),"focused seed editor was not mounted");
    const auto bounds=take(editor.snapshot()).bounds;
    good(state->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=bounds.x+8,.y=bounds.y+10,.buttons=1}));
    good(state->input({.kind=ii::gui::WidgetInputKind::pointer_move,.x=bounds.x+70,.y=bounds.y+10,.buttons=1}));
    good(state->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=bounds.x+70,.y=bounds.y+10}));
    check(take(editor.snapshot()).text_selection.length>0 && row(view,"Seed")->text.get()=="123456789",
          "read-only inspector text could not be selected without editing the result");
    const std::string longPrompt(100000,'p');
    const vm::Value longResult(vm::Object{{"id",vm::Value("a")},{"imageSource",vm::Value("file:///a.png")},
        {"documentSource",vm::Value("file:///a.iisc")},{"seed",vm::Value("123456789")},{"prompt",vm::Value(longPrompt)}});
    session->replaceEntries({longResult});
    (void)take(state->drawing_snapshot({320,706}));
    auto longEditor=find(state,"inspectorLongFieldText");
    const auto longBounds=take(longEditor.snapshot()).bounds;
    good(state->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=longBounds.x+8,.y=longBounds.y+10,.buttons=1}));
    good(state->input({.kind=ii::gui::WidgetInputKind::pointer_move,.x=longBounds.x+70,.y=longBounds.y+10,.buttons=1}));
    good(state->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=longBounds.x+70,.y=longBounds.y+10}));
    check(take(longEditor.snapshot()).text_selection.length>0 && row(view,"Prompt")->text.get()==longPrompt,
          "long document-backed inspector text could not be selected without truncation");
    state->close();
    int mediaClicks=0;
    auto media=ii::gui::compiled_view_source([&](ii::gui::WidgetBuilder &builder) -> ii::core::Result<void> {
        auto button=builder.button({},"mediaControl",{.text="Play"},[&] { ++mediaClicks; });
        if (!button) return std::unexpected(button.error());
        return {};
    });
    state=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();});
    root=take(state->content_host({},"mediaRoot",{320,706}));
    good(root.set_content([&](ii::gui::WidgetBuilder &builder){return review::buildReviewInspector(builder,{},view,{},media);}));
    (void)take(state->drawing_snapshot({320,706}));
    good(find(state,"mediaControl").activate());
    check(mediaClicks==1,"injected media view did not retain its controls");
    state->close();
}
#endif
}
int main() {
    try { exercise();
#ifdef DREAMSCAPES_REVIEW_INSPECTOR_WIDGET_TEST
        widgets();
#endif
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
