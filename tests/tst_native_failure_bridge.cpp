#include "App/Generation/NativeFailurePresenter.h"
#include <iostream>
#include <stdexcept>
#include <thread>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
int main() {
    try {
        using namespace dreamscapes;
        auto model=std::make_shared<failure::FailureDetailsModel>();
        failure::NativeFailurePresenter presenter(model);
        generation::QueueJob job("one",{"Image","model",vm::Value(vm::Object{})});
        job.state.set(generation::QueueState::Failed);job.error.set("원문 오류");
        check(bool(presenter.open(job)) && model->open.get() &&
            model->reportText().find("원문 오류")!=std::string::npos,
            "native presenter did not open the selected job report");
        std::string copy;
        model->setClipboardWriter([&](std::string_view text){copy=text;return ii::core::Result<void>{};});
        model->copyReport();check(copy==model->reportText(),"native presenter lost selectable clipboard text");
        bool rejectedWorker=false;
        std::thread worker([&] {auto result=presenter.open(job);rejectedWorker=!result && result.error().code==ii::core::ErrorCode::wrong_executor;});
        worker.join();check(rejectedWorker,"native presenter accepted a foreign thread");
        model->dismiss();job.state.set(generation::QueueState::Canceled);job.error.set("Recoverable output");
        check(bool(presenter.open(job)) && model->title.get()=="Generation details" &&
            model->reportText().find("Recoverable output")!=std::string::npos,
            "native presenter lost canceled recovery details");
        model.reset();check(!presenter.open(job),"native presenter retained a destroyed model");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
