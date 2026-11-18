#include "App/Generation/NativeGenerationQueue.h"
#include "App/Views/Result/NativeGenerationReviewBridge.h"
#include <iiSDKCore>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
template<class T> T take(ii::core::Result<T> value) {
    if (!value) throw std::runtime_error(value.error().message);return std::move(*value);
}
int main() {
    try {
        using namespace dreamscapes;
        ii::runtime::EventLoop loop;std::vector<std::function<void()>> tasks;
        std::shared_ptr<review::QueueModel> display;
        auto queue=generation::Queue::create(loop,[&](auto task){tasks.push_back(std::move(task));return true;},
            [&](const auto &request,const auto &canceled,const auto &progress,const auto &output) {
                progress({"denoising",2,7});(void)loop.poll();
                check(std::ranges::any_of(display->items.get(),[](const auto &item) {return item->showProgress.get() &&
                      item->step.get()==2 && item->total.get()==7;}),"actual progress did not reach the queue view");
                output(vm::Value(vm::Object{{"path",vm::Value(request.model+".png")},{"isImage",vm::Value(true)}}));
                return generation::QueueCompletion{canceled.load()?generation::QueueState::Canceled:generation::QueueState::Completed,{}};
            });
        auto session=std::make_shared<review::Session>();
        display=review::QueueModel::create(session);
        auto home=std::make_shared<home::HomeQueueModel>();
        auto bridge=review::GenerationReviewBridge::create(queue,session,display,home);
        auto first=take(queue->enqueue({"Image","first",vm::Value(vm::Object{})}));
        auto next=take(queue->enqueue({"Image","next",vm::Value(vm::Object{})}));
        check(display->running.get()==1 && display->queued.get()==1 && home->execution.get()=="Running",
              "queue status did not reach the native views");
        display->items.get()[1]->cancel();
        check(next->state.get()==generation::QueueState::Canceled && display->canceled.get()==1,
              "native queue cancellation did not reach the generation service");
        auto task=std::move(tasks.front());tasks.clear();task();(void)loop.poll();
        check(first->state.get()==generation::QueueState::Completed && display->completed.get()==1 &&
              session->totalCount()==1 && vm::text(vm::get(session->entryAt(0),"jobId"))==first->id &&
              vm::text(vm::get(session->entryAt(0),"path"))=="first.png",
              "a committed output did not reach the actual review session");
        display->items.get().front()->select();
        check(session->jobFilter()==first->id && session->visibleCount()==1,"job selection did not filter the review session");
        session->selectAt(0);const auto selected=session->selectedCount();
        auto later=take(queue->enqueue({"Image","later",vm::Value(vm::Object{})}));
        task=std::move(tasks.front());tasks.clear();task();(void)loop.poll();
        check(later->state.get()==generation::QueueState::Completed && session->totalCount()==2 &&
              session->selectedCount()==selected && session->jobFilter()==first->id && session->visibleCount()==1,
              "a new output changed the active selection or job filter");
        display->showAllJobs();check(session->visibleCount()==2,"All jobs did not restore the full results list");
        bridge->close();queue->close();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
