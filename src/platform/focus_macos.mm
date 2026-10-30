#import <AppKit/AppKit.h>

#include "focus.h"

static void (*g_deactivate_cb)() = nullptr;

void ap_set_app_deactivate_cb(void (*cb)()) {
    g_deactivate_cb = cb;
    [[NSNotificationCenter defaultCenter]
        addObserverForName:NSApplicationWillResignActiveNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification*) {
                    if (g_deactivate_cb) g_deactivate_cb();
                }];
}
