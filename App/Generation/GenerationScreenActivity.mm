#include "GenerationScreenActivity.h"
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <Generation/NativeDiffusion.hpp>
#import <UIKit/UIKit.h>

namespace {
struct ScreenActivity {
    id memoryObserver = [NSNotificationCenter.defaultCenter
        addObserverForName:UIApplicationDidReceiveMemoryWarningNotification object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) {
            if (std::getenv("IILD_NATIVE_DIAGNOSTICS"))
                std::fputs("Dreamscapes: UIKit memory warning; releasing native model cache\n", stderr);
            iiLocalDiffusion::releaseNativeDiffusionCache();
        }];
    bool engaged = false;
    BOOL previous = NO;
    void set(bool active) {
        if (engaged == active) return;
        if (active) {
            previous = UIApplication.sharedApplication.idleTimerDisabled;
            UIApplication.sharedApplication.idleTimerDisabled = YES;
        } else {
            UIApplication.sharedApplication.idleTimerDisabled = previous;
        }
        engaged = active;
    }
    ~ScreenActivity() {
        [NSNotificationCenter.defaultCenter removeObserver:memoryObserver];
        set(false);
        iiLocalDiffusion::releaseNativeDiffusionCache();
    }
};
}

std::function<void(bool)> nativeGenerationScreenActivity()
{
    auto activity = std::make_shared<ScreenActivity>();
    return [activity](bool active) { activity->set(active); };
}
