#import <AppKit/AppKit.h>
#include "apps.h"
#include "wm.h"

// NSWorkspace app launch / quit / activate / hide events, and the app queries (name, icon,
// hide) that need AppKit.

static void add(NSRunningApplication *app) {
    if (app.activationPolicy != NSApplicationActivationPolicyRegular) return;
    wm_add_app(app.processIdentifier, app.bundleIdentifier.UTF8String);
}

static void add_running(void) {
    for (NSRunningApplication *app in NSWorkspace.sharedWorkspace.runningApplications) add(app);
}

// A start under a lock tracked no window, so once the screen unlocks every app is looked at again: the ones
// that did not register, then the windows of all of them.
static void apps_rescan(void) {
    @autoreleasepool {
        wm_set_trigger("unlock"); // the windows tracked while apps are added log it too
        add_running();
        wm_rescan();
    }
}

// The main queue, like every other event. Registered with DeliverImmediately: the other suspension behaviours
// drop or hold a notification while the app is not the active one, and this one never is.
static void screen_unlocked(CFNotificationCenterRef center, void *observer, CFNotificationName name, const void *object,
                            CFDictionaryRef info) {
    (void)center;
    (void)observer;
    (void)name;
    (void)object;
    (void)info;
    dispatch_async(dispatch_get_main_queue(), ^{ apps_rescan(); });
}

@interface JDAppObserver : NSObject
@end

@implementation JDAppObserver
- (void)launched:(NSNotification *)n {
    add(n.userInfo[NSWorkspaceApplicationKey]);
}
- (void)terminated:(NSNotification *)n {
    wm_set_trigger("terminated");
    NSRunningApplication *app = n.userInfo[NSWorkspaceApplicationKey];
    wm_settle_pid(app.processIdentifier);
    wm_remove_app(app.processIdentifier);
    wm_on_focus();
}
- (void)activated:(NSNotification *)n {
    (void)n;
    wm_set_trigger("activated");
    wm_on_focus();
}
- (void)hidden:(NSNotification *)n {
    (void)n;
    wm_refresh_ui();
}
@end

static JDAppObserver *observer;

void apps_init(void) {
    observer = [JDAppObserver new];
    NSNotificationCenter *nc = NSWorkspace.sharedWorkspace.notificationCenter;
    [nc addObserver:observer selector:@selector(launched:) name:NSWorkspaceDidLaunchApplicationNotification object:nil];
    [nc addObserver:observer selector:@selector(terminated:) name:NSWorkspaceDidTerminateApplicationNotification object:nil];
    [nc addObserver:observer selector:@selector(activated:) name:NSWorkspaceDidActivateApplicationNotification object:nil];
    [nc addObserver:observer selector:@selector(hidden:) name:NSWorkspaceDidHideApplicationNotification object:nil];
    [nc addObserver:observer selector:@selector(hidden:) name:NSWorkspaceDidUnhideApplicationNotification object:nil];
    // Posted when the lock screen goes away. CoreFoundation's center, which needs no AppKit class.
    CFNotificationCenterAddObserver(CFNotificationCenterGetDistributedCenter(), NULL, screen_unlocked,
                                    CFSTR("com.apple.screenIsUnlocked"), NULL, CFNotificationSuspensionBehaviorDeliverImmediately);
    add_running();
}

pid_t apps_frontmost(void) {
    return NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
}

CGImageRef apps_icon(pid_t pid, int px) {
    @autoreleasepool {
        NSImage *img = [NSRunningApplication runningApplicationWithProcessIdentifier:pid].icon;
        NSRect r = NSMakeRect(0, 0, px, px);
        CGImageRef src = [img CGImageForProposedRect:&r context:nil hints:nil];
        if (!src) return NULL;
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef c = CGBitmapContextCreate(NULL, px, px, 8, 0, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
        CGColorSpaceRelease(cs);
        if (!c) return NULL;
        CGContextSetInterpolationQuality(c, kCGInterpolationHigh);
        CGContextDrawImage(c, CGRectMake(0, 0, px, px), src);
        CGImageRef out = CGBitmapContextCreateImage(c);
        CGContextRelease(c);
        return out;
    }
}

CFStringRef apps_name(pid_t pid) {
    @autoreleasepool {
        return (CFStringRef)CFBridgingRetain([NSRunningApplication runningApplicationWithProcessIdentifier:pid].localizedName);
    }
}

bool apps_alive(pid_t pid) {
    @autoreleasepool {
        NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
        return app && !app.isTerminated;
    }
}

bool apps_hide(pid_t pid) {
    @autoreleasepool {
        NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
        if (!app || app.isTerminated || app.isHidden) return false;
        // -hide can answer NO for the active app and still hide it.
        [app hide];
        return true;
    }
}

bool apps_hidden(pid_t pid) {
    @autoreleasepool {
        return [NSRunningApplication runningApplicationWithProcessIdentifier:pid].isHidden;
    }
}
