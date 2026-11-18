#include "App/Generation/NativeGenerationQueue.h"
#include <iiSDKCore>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
template<class T> T take(ii::core::Result<T> value) {
    if (!value) throw std::runtime_error(value.error().message);
    return std::move(*value);
}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
}

int main() {
    try {
        using namespace dreamscapes;
        using namespace dreamscapes::generation;
        ii::runtime::EventLoop loop;
        std::vector<std::function<void()>> workers;
        std::vector<std::string> launched;
        QueueOutputCallback lateOutput;
        bool reject=false;
        auto submit=[&](std::function<void()> task) {
            if (reject) return false;
            workers.push_back(std::move(task));return true;
        };
        auto run=[&](const QueueRequest &request,const QueueCancellation &canceled,
                     const QueueProgressCallback &progress,const QueueOutputCallback &output) {
            launched.push_back(request.model);
            if (canceled.load()) {
                check(canceled.token().stop_requested(),"worker cancellation did not signal the process stop token");
                return QueueCompletion{QueueState::Canceled,{}};
            }
            progress({"loading",0,0});
            progress({"denoising",3,10});
            progress({std::string("invalid\0stage",13),1,1});
            progress({std::string("\xff",1),1,1});
            progress({"invalid",11,10});
            lateOutput=output;
            output(vm::Value{});
            output(vm::Value(vm::Object{{"path",vm::Value(request.model+".png")}}));
            if (request.model=="failure") return QueueCompletion{QueueState::Failed,"Engine rejected the model",
                {"Engine rejected the model","worker trace",7,false}};
            return QueueCompletion{QueueState::Completed,{}};
        };
        auto queue=Queue::create(loop,submit,run);
        auto first=take(queue->enqueue({"Image","first",vm::Value(vm::Object{{"steps",vm::Value(10)}})}));
        auto canceled=take(queue->enqueue({"Image","never",vm::Value(vm::Object{})}));
        auto failure=take(queue->enqueue({"Image","failure",vm::Value(vm::Object{})}));
        bool evidenceBeforeFailure=false;
        const auto observation=failure->state.observe([&](QueueState state,std::uint64_t) {
            if (state==QueueState::Failed) evidenceBeforeFailure=failure->evidence.get().workerLog=="worker trace";
        });
        auto last=take(queue->enqueue({"Image","last",vm::Value(vm::Object{})}));
        check(queue->jobs.get().size()==4 && first->state.get()==QueueState::Running &&
              canceled->state.get()==QueueState::Queued && workers.size()==1,"jobs did not enter a serial queue");
        good(queue->cancel(canceled->id));
        check(canceled->state.get()==QueueState::Canceled,"waiting cancellation was not applied");
        auto task=std::move(workers.front());workers.erase(workers.begin());task();(void)loop.poll();
        check(first->state.get()==QueueState::Completed && first->progress.get().stage=="denoising" &&
              first->progress.get().completed==3 && first->progress.get().total==10 &&
              queue->outputs.get().size()==1 && workers.size()==1,"first job did not publish its actual progress and output");
        lateOutput(vm::Value(vm::Object{{"path",vm::Value("late.png")}}));(void)loop.poll();
        check(queue->outputs.get().size()==1,"a callback retained past completion published a late output");
        task=std::move(workers.front());workers.erase(workers.begin());task();(void)loop.poll();
        check(failure->state.get()==QueueState::Failed && failure->error.get()=="Engine rejected the model" && evidenceBeforeFailure &&
              queue->outputs.get().size()==2 && workers.size()==1,"a failed job lost its completed output or stalled the queue");
        task=std::move(workers.front());workers.erase(workers.begin());task();(void)loop.poll();
        check(last->state.get()==QueueState::Completed && queue->outputs.get().size()==3 &&
              launched==std::vector<std::string>{"first","failure","last"},"later jobs did not survive cancellation or failure");
        check(queue->outputs.get()[0].jobId==first->id &&
              vm::text(vm::get(queue->outputs.get()[0].entry,"path"))=="first.png" &&
              queue->outputs.get()[1].jobId==failure->id &&
              vm::text(vm::get(queue->outputs.get()[1].entry,"path"))=="failure.png", "outputs lost their arrival order or job identity");
        auto running=take(queue->enqueue({"Image","cancel-running",vm::Value(vm::Object{})}));
        good(queue->cancel(running->id));
        task=std::move(workers.front());workers.erase(workers.begin());task();(void)loop.poll();
        check(running->state.get()==QueueState::Canceled && queue->outputs.get().size()==3,
              "running cancellation did not reach the worker");
        auto blocker=take(queue->enqueue({"Image","blocker",vm::Value(vm::Object{})}));
        auto reentrant=take(queue->enqueue({"Image","reentrant",vm::Value(vm::Object{})}));
        const auto cancellation=reentrant->state.observe([&](QueueState state,std::uint64_t) {
            if (state==QueueState::Running) good(queue->cancel(reentrant->id));
        });
        task=std::move(workers.front());workers.erase(workers.begin());task();(void)loop.poll();
        check(blocker->state.get()==QueueState::Completed && reentrant->state.get()==QueueState::Canceled && workers.empty() &&
              queue->outputs.get().size()==4,"reentrant cancellation launched a retired job");
        bool refusedThread=false;
        std::thread other([&] {refusedThread=!queue->enqueue({"Image","wrong-thread",vm::Value(vm::Object{})});});
        other.join();check(refusedThread,"generation queue accepted a mutation from another thread");
        reject=true;
        auto unavailable=take(queue->enqueue({"Image","unavailable",vm::Value(vm::Object{})}));
        check(unavailable->state.get()==QueueState::Failed && !unavailable->error.get().empty(),
              "worker rejection was represented as a successful job");
        reject=false;
        auto retired=take(queue->enqueue({"Image","retired",vm::Value(vm::Object{})}));
        queue->close();
        task=std::move(workers.front());workers.erase(workers.begin());task();(void)loop.poll();
        check(retired->state.get()!=QueueState::Completed && queue->outputs.get().size()==4,
              "late worker events modified a closed queue");
        check(!queue->enqueue({"Image","closed",vm::Value(vm::Object{})}),"closed queue accepted a job");

        std::vector<std::function<void()>> logWorkers;
        auto logQueue=Queue::create(loop,[&](auto value){logWorkers.push_back(std::move(value));return true;},
            [](const auto &,const auto &,const auto &,const auto &) {
                std::string text="first cause\n";
                for (int i=0;i<30000;++i) text+="한";
                text+="\nlast causal event";text.append("\0\xff",2);
                return QueueCompletion{QueueState::Failed,std::move(text)};
            });
        auto logged=take(logQueue->enqueue({"Image","logs",vm::Value(vm::Object{})}));
        logWorkers.front()();(void)loop.poll();
        check(logged->state.get()==QueueState::Failed && logged->error.get().size()<=65536 &&
            logged->error.get().find("first cause")!=std::string::npos &&
            logged->error.get().find("last causal event")!=std::string::npos &&
            ii::core::valid_utf8(logged->error.get()) && logged->error.get().find('\0')==std::string::npos,
            "generation queue lost the failure log tail or produced unselectable text");
        logQueue->close();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
