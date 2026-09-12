#include "GenerationBackgroundActivity.h"
#include <QDebug>
#include <algorithm>
#import <BackgroundTasks/BackgroundTasks.h>
#import <UIKit/UIKit.h>

namespace {
class BackgroundActivity final : public GenerationBackgroundActivity,
                                 public std::enable_shared_from_this<BackgroundActivity> {
public:
    ~BackgroundActivity() override {
        [NSNotificationCenter.defaultCenter removeObserver:m_backgroundObserver];
        [NSNotificationCenter.defaultCenter removeObserver:m_foregroundObserver];
        end(false);
    }

    void observeForeground(std::function<void(bool)> changed) override {
        m_foregroundChanged = std::move(changed);
        const auto weak = weak_from_this();
        m_backgroundObserver = [NSNotificationCenter.defaultCenter
            addObserverForName:UIApplicationDidEnterBackgroundNotification object:nil
            queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) {
                if (auto self = weak.lock()) self->m_foregroundChanged(false);
            }];
        m_foregroundObserver = [NSNotificationCenter.defaultCenter
            addObserverForName:UIApplicationWillEnterForegroundNotification object:nil
            queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) {
                if (auto self = weak.lock()) {
                    self->ensureCleanup();
                    self->m_foregroundChanged(true);
                }
            }];
        // UIKit is authoritative: a notification panel, alert, or Control Center
        // may obscure Qt's window without putting the application in the background.
        m_foregroundChanged(UIApplication.sharedApplication.applicationState != UIApplicationStateBackground);
    }

    void begin(const QString &job, std::function<void()> expired) override {
        end(false);
        m_identifier = [@"com.iisacc.dreamscapes.generation." stringByAppendingString:job.toNSString()];
        m_expired = std::move(expired);
        m_state = "foreground-only";
        m_error.clear();
        m_progress = 0;
        const auto weak = weak_from_this();
        NSString *identifier = m_identifier;
        ensureCleanup();
        if (@available(iOS 26.0, *)) {
            m_gpuSupported = (BGTaskScheduler.supportedResources & BGContinuedProcessingTaskRequestResourcesGPU) != 0;
            if (!m_gpuSupported) { m_state = "gpu-unavailable"; return; }
            // Register the concrete UUID, authorized by the plist's wildcard.
            // Each identifier is registered only once, including after retries.
            const BOOL registered = [BGTaskScheduler.sharedScheduler registerForTaskWithIdentifier:identifier
                usingQueue:dispatch_get_main_queue() launchHandler:^(__kindof BGTask *task) {
                    auto self = weak.lock();
                    if (!self || ![self->m_identifier isEqualToString:identifier] || !self->m_expired) {
                        [task setTaskCompletedWithSuccess:NO];
                        return;
                    }
                    self->m_task = task;
                    self->m_state = "running";
                    task.expirationHandler = ^{
                        dispatch_async(dispatch_get_main_queue(), ^{
                            if (auto current = weak.lock(); current && [current->m_identifier isEqualToString:identifier])
                                current->expire();
                        });
                    };
                    auto *continued = (BGContinuedProcessingTask *)task;
                    continued.progress.totalUnitCount = 10000;
                    continued.progress.completedUnitCount = self->m_progress;
                    qInfo("Dreamscapes: background GPU generation granted");
                    self->m_foregroundChanged(UIApplication.sharedApplication.applicationState != UIApplicationStateBackground);
                }];
            if (!registered) { m_state = "registration-failed"; return; }
            auto *request = [[BGContinuedProcessingTaskRequest alloc] initWithIdentifier:identifier
                title:NSLocalizedString(@"Generating image", nil)
                subtitle:NSLocalizedString(@"Preparing model…", nil)];
            request.requiredResources = BGContinuedProcessingTaskRequestResourcesGPU;
            // Foreground inference starts immediately. Never enqueue a second,
            // delayed system task for work that may already have finished.
            request.strategy = BGContinuedProcessingTaskRequestSubmissionStrategyFail;
            m_state = "requested";
            const auto submitted = ^(NSError *error) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    auto self = weak.lock();
                    if (!self || ![self->m_identifier isEqualToString:identifier]) {
                        [BGTaskScheduler.sharedScheduler cancelTaskRequestWithIdentifier:identifier];
                        return;
                    }
                    if (error) {
                        self->m_state = "denied";
                        self->m_error = QString::fromNSString(error.localizedDescription);
                        qWarning() << "Dreamscapes background request:" << self->m_error;
                    }
                });
            };
            // iOS 27 reports additional submission failures asynchronously.
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
                if (@available(iOS 27.0, *)) {
                    [BGTaskScheduler.sharedScheduler submitTaskRequest:request completionHandler:submitted];
                } else {
                    NSError *error = nil;
                    [BGTaskScheduler.sharedScheduler submitTaskRequest:request error:&error];
                    submitted(error);
                }
            });
        }
    }

    bool allowsBackgroundExecution() const override { return m_task && m_state == "running"; }

    void update(const iiLocalDiffusion::NativeGenerationProgress &event) override {
        using Stage = iiLocalDiffusion::NativeGenerationStage;
        const int fraction = event.total > 0 ? std::clamp(event.step * 1000 / event.total, 0, 1000) : 0;
        QString subtitle;
        int units = 0;
        switch (event.stage) {
        case Stage::Waiting: subtitle = QStringLiteral("Waiting for image engine…"); break;
        case Stage::Preparing: units = fraction * 2; subtitle = QStringLiteral("Preparing model…"); break;
        case Stage::Loading: units = 2000 + fraction; subtitle = QStringLiteral("Loading model…"); break;
        case Stage::Encoding: units = 3000; subtitle = QStringLiteral("Reading prompt…"); break;
        case Stage::Denoising:
            units = 3500 + fraction * 5;
            subtitle = QStringLiteral("Step %1 of %2").arg(event.step).arg(event.total);
            break;
        case Stage::Decoding: units = 8500 + fraction; subtitle = QStringLiteral("Rendering image…"); break;
        }
        // Model loading/VAE callbacks may revisit a phase. Never move system
        // progress backwards or report success before the image is saved.
        m_progress = std::max(m_progress, units);
        if (@available(iOS 26.0, *)) {
            if (m_task) {
                auto *continued = (BGContinuedProcessingTask *)m_task;
                continued.progress.completedUnitCount = m_progress;
                [continued updateTitle:NSLocalizedString(@"Generating image", nil) subtitle:subtitle.toNSString()];
            }
        }
    }

    void end(bool success) override {
        if (m_task) {
            if (@available(iOS 26.0, *)) {
                auto *continued = (BGContinuedProcessingTask *)m_task;
                if (success) continued.progress.completedUnitCount = continued.progress.totalUnitCount;
            }
            m_task.expirationHandler = nil;
            [m_task setTaskCompletedWithSuccess:success];
            m_task = nil;
        }
        if (m_identifier) [BGTaskScheduler.sharedScheduler cancelTaskRequestWithIdentifier:m_identifier];
        m_identifier = nil;
        m_expired = {};
        endCleanup();
        m_state = "idle";
    }

    QVariantMap status() const override {
        return {{"state", m_state}, {"gpuSupported", m_gpuSupported},
                {"allowsBackgroundExecution", allowsBackgroundExecution()},
                {"cleanupAssertion", m_cleanupTask != UIBackgroundTaskInvalid},
                {"progress", m_progress}, {"error", m_error}};
    }

private:
    void ensureCleanup() {
        if (!m_identifier || m_cleanupTask != UIBackgroundTaskInvalid) return;
        const auto weak = weak_from_this();
        NSString *identifier = m_identifier;
        // Renew on foreground resumption, so another app switch can drain the
        // current compute segment even after the previous assertion expired.
        m_cleanupTask = [UIApplication.sharedApplication
            beginBackgroundTaskWithName:@"Finish Dreamscapes generation" expirationHandler:^{
                if (auto self = weak.lock(); self && [self->m_identifier isEqualToString:identifier]) {
                    self->endCleanup();
                    // A denied GPU job is parked already. Allow suspension and
                    // keep its in-memory request, without reporting cancellation.
                }
            }];
    }
    void endCleanup() {
        if (m_cleanupTask == UIBackgroundTaskInvalid) return;
        const auto task = m_cleanupTask;
        m_cleanupTask = UIBackgroundTaskInvalid;
        [UIApplication.sharedApplication endBackgroundTask:task];
    }
    void expire() {
        if (!m_expired) return;
        m_state = "expired";
        auto callback = std::move(m_expired);
        callback();
        // The worker acknowledges cooperative cancellation through end(false).
        // The finite UIKit assertion permits it to unwind and release resources.
    }

    id m_backgroundObserver = nil;
    id m_foregroundObserver = nil;
    BGTask *m_task = nil;
    NSString *m_identifier = nil;
    UIBackgroundTaskIdentifier m_cleanupTask = UIBackgroundTaskInvalid;
    std::function<void(bool)> m_foregroundChanged;
    std::function<void()> m_expired;
    QString m_state = "idle";
    QString m_error;
    bool m_gpuSupported = false;
    int m_progress = 0;
};
}

std::shared_ptr<GenerationBackgroundActivity> nativeGenerationBackgroundActivity()
{
    return std::make_shared<BackgroundActivity>();
}
