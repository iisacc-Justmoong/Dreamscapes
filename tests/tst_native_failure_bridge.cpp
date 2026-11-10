#include "App/Generation/NativeFailureDetailsBridge.h"
#include "App/Generation/GenerationController.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
void exercise() {
    QTemporaryDir storage(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/native-failure-bridge-XXXXXX"));
    check(storage.isValid(), "Could not create isolated fixture");
    GenerationRuntime runtime;
    runtime.modelPreferencesFile = storage.filePath("models.conf");
    runtime.failureReportsDirectory = storage.path();
    auto controller = std::make_unique<GenerationController>(runtime);
    auto model = std::make_shared<dreamscapes::failure::FailureDetailsModel>();
    dreamscapes::failure::NativeFailureDetailsBridge bridge(controller.get(), model);
    check(bool(bridge.openReport("job-one")) && model->open.get(), "Opening the controller report failed");
    const QVariantMap pending{{"jobId", "job-one"}, {"state", "pending"}, {"rawError", "오류"}};
    controller->failureExplained("job-one", pending);
    check(model->reportText().find("오류") != std::string::npos &&
              model->reportText().find("Agent analysis is running.") == 0,
          "The real controller signal did not reach the native report");
    controller->failureExplained("job-other", {{"jobId", "job-other"}, {"details", "wrong"}});
    check(model->reportText().find("오류") != std::string::npos, "Another job replaced the active report");
    const QVariantMap complete{{"jobId", "job-one"}, {"details", "Final diagnosis"}, {"reportPath", "/fixture/report.json"}};
    controller->failureExplained("job-one", complete);
    check(model->reportText() == "Final diagnosis\n\nReport file: /fixture/report.json",
          "The completed real-controller report did not replace pending evidence");
    controller->failureExplained("job-one", {{"jobId", "job-one"}, {"details", QString::fromUtf8("a\0b", 3)}});
    check(model->reportFailed.get() && !bridge.lastError().empty() && model->reportText().find("Final diagnosis") == 0,
          "Invalid asynchronous evidence was silently accepted or discarded the previous report");
    controller->failureExplained("job-one", complete);
    check(!model->reportFailed.get() && bridge.lastError().empty(), "The report did not recover after valid evidence");
    bool rejectedWorker = false;
    std::thread worker([&] {
        const auto result = bridge.openReport("job-one");
        rejectedWorker = !result && result.error().code == ii::core::ErrorCode::wrong_executor;
    });
    worker.join();
    check(rejectedWorker, "The controller bridge accepted a foreign thread");
    controller.reset();
    check(!bridge.openReport("job-one") && model->reportText().find("Final diagnosis") == 0,
          "Destroyed controller damaged retained evidence or allowed a stale request");
}
}
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    try { exercise(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
