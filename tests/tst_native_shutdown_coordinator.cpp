#include "App/ApplicationShutdownCoordinator.h"
#include <iiSDKCore>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void check(bool value,const char *message) { if (!value) throw std::runtime_error(message); }
void exercise() {
    ii::runtime::EventLoop loop;
    ApplicationShutdownCoordinator shutdown(loop);
    std::vector<std::string> order;
    std::function<void()> delayed;
    bool finished=false, success=true, ownerThread=false;
    check(shutdown.add([&](auto done) {order.push_back("first");done();done();}),"first registration failed");
    check(shutdown.add([&](auto done) {order.push_back("delayed");delayed=std::move(done);}),"delayed registration failed");
    check(shutdown.add([&](auto) {order.push_back("failure");throw std::runtime_error("fixture failure");}),"failure registration failed");
    auto started=shutdown.begin([&](auto result) {
        finished=true;success=bool(result);ownerThread=shutdown.isOwnerThread();
    });
    check(bool(started) && shutdown.isClosing() && !finished,"shutdown did not await the delayed operation");
    int waiting=0;
    check(bool(shutdown.begin([&](auto result) {if (!result) ++waiting;})),"a second waiter was rejected");
    check(order==std::vector<std::string>({"first","delayed","failure"}),"shutdown lost its registration order");
    check(!shutdown.add([](auto done){done();}),"shutdown accepted late work");
    std::thread worker([&] {delayed();delayed();}); worker.join();
    check(!finished,"worker thread delivered completion directly");
    (void)loop.poll();
    check(finished && !success && ownerThread && waiting==1,"completion or error was not delivered on the owner loop");
    int again=0;
    check(bool(shutdown.begin([&](auto result) {check(!result,"shutdown lost its original failure");++again;})),
          "completed shutdown could not be observed again");
    check(again==1,"completed shutdown did not notify its observer");
    ApplicationShutdownCoordinator empty(loop);
    bool emptyDone=false;check(bool(empty.begin([&](auto r) {emptyDone=bool(r);})),"empty shutdown failed");
    check(emptyDone,"empty shutdown did not complete");
    ApplicationShutdownCoordinator ownerOnly(loop);
    bool rejected=false;
    std::thread foreign([&] {rejected=!ownerOnly.add([](auto done){done();});}); foreign.join();
    check(rejected,"foreign thread registered an owner-thread handler");
    ii::runtime::EventLoop full(1);
    auto reserved=full.on_signal([] {});check(bool(reserved),"fixture could not reserve the event slot");
    ApplicationShutdownCoordinator retry(full);
    check(retry.add([](auto done) {done();}),"retry task could not be registered");
    check(!retry.begin() && !retry.isClosing(),"event admission failure started shutdown");
    check(full.cancel(reserved.token),"fixture could not release the event slot");
    bool retried=false;
    check(bool(retry.begin([&](auto result) {retried=bool(result);})) && retried,
          "shutdown could not retry after event capacity recovered");
    std::function<void()> afterDestruction;
    {
        ApplicationShutdownCoordinator abandoned(loop);
        check(abandoned.add([&](auto done) {afterDestruction=std::move(done);}),"abandoned task registration failed");
        check(bool(abandoned.begin()),"abandoned shutdown did not start");
    }
    std::thread late([&] {afterDestruction();}); late.join();
    (void)loop.poll();
}
}
int main() {
    try {exercise();}
    catch (const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
}
