#include "App/Views/Result/NativeGenerationReviewWorkspace.h"
#include "NativeFailureTestHelpers.h"
#include <iiGUI/WidgetState.h>
#include <iiSDKCore>
#include <iostream>
#include <stdexcept>

template<class T> T take(ii::core::Result<T> value) {if (!value) throw std::runtime_error(value.error().message);return std::move(*value);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;
        ii::runtime::EventLoop loop;
        auto session=std::make_shared<review::Session>();auto queue=review::QueueModel::create(session);
        auto failure=std::make_shared<failure::FailureDetailsModel>();failure->motionEnabled.set(false);
        auto model=review::GenerationReviewWorkspace::create(session,queue,{},failure,{},{});
        ii::gui::detail::WidgetFrameHooks hooks;
        hooks.events=[&]() -> ii::core::Result<ii::runtime::EventLoop *> {return &loop;};
        auto tree=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();},ii::gui::WidgetAppearance{},hooks);
        auto root=take(tree->content_host({},"reviewRoot",{1200,800}));
        good(root.set_content([&](ii::gui::WidgetBuilder &builder) {return review::buildGenerationReviewWorkspace(builder,{},model);}));
        (void)take(tree->drawing_snapshot({1200,800}));
        bool foundQueue=false,foundInspector=false;ii::gui::Widget host;
        for (const auto &node:take(tree->snapshots())) {
            foundQueue|=node.id.ends_with("/reviewQueue");foundInspector|=node.id.ends_with("/reviewInspector");
            if (node.id.ends_with("/generationFailureView/@native-content")) host=take(tree->find(node.id));
        }
        if (!foundQueue || !foundInspector) throw std::runtime_error("generation review did not mount queue and inspector: width="+
            std::to_string(model->width.get())+" queue="+std::to_string(model->showQueue.get())+
            " inspector="+std::to_string(model->showInspector.get()));
        auto modal=take(host.content_item("failureDetails"));
        good(failure->setReport(vm::Value(vm::Object{{"details",vm::Value("Actual failure evidence")}})));
        failure->show();(void)take(tree->drawing_snapshot({1200,800}));
        auto branch=take(failureReportBranch(modal,"failureExplanationText"));
        auto field=take(branch.content_item("failureExplanationText"));
        auto snapshot=take(field.snapshot());
        if (!snapshot.read_only || take(field.text_content({0,snapshot.text_length})).text!="Actual failure evidence")
            throw std::runtime_error("generation failure popup did not expose selectable text");
        good(take(modal.popup_content("failureDetails.action-0")).activate());
        tree->close();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
