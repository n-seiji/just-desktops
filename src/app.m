#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>
#include "app.h"
#include "config_write.h"
#include <dlfcn.h>
#include <spawn.h>

// Everything the .app does for a user without a terminal: the first-run permission panel, the
// login item, the config file and About. Only reached when jd_app_mode() is true; ServiceManagement is loaded on first use.

extern char **environ;

static void alert(NSString *title, NSString *info) {
    [NSApp activateIgnoringOtherApps:YES];
    NSAlert *a = [NSAlert new];
    a.messageText = title;
    a.informativeText = info;
    [a runModal];
}

@interface JDAccessTarget : NSObject
@end

@implementation JDAccessTarget
- (void)openSettings:(id)sender {
    (void)sender;
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"]];
}
- (void)quit:(id)sender {
    (void)sender;
    jd_quit();
}
@end

static JDAccessTarget *access_target;

// Polls once a second like the command line does, but pumps AppKit events so the buttons work.
void app_wait_accessibility(void) {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp finishLaunching];
    access_target = [JDAccessTarget new];

    NSTextField *title = [NSTextField labelWithString:@"just-desktops needs Accessibility permission"];
    title.font = [NSFont boldSystemFontOfSize:15];
    NSTextField *body = [NSTextField wrappingLabelWithString:
        @"just-desktops needs Accessibility permission to move and focus windows.\n\n"
        @"Click Open System Settings and turn on just-desktops under Privacy & Security > Accessibility. "
        @"It starts by itself once the permission is on."];
    body.preferredMaxLayoutWidth = 360;
    NSButton *open = [NSButton buttonWithTitle:@"Open System Settings" target:access_target action:@selector(openSettings:)];
    open.keyEquivalent = @"\r";
    NSButton *quit = [NSButton buttonWithTitle:@"Quit" target:access_target action:@selector(quit:)];
    NSStackView *buttons = [NSStackView stackViewWithViews:@[ quit, open ]];
    buttons.spacing = 10;
    NSStackView *stack = [NSStackView stackViewWithViews:@[ title, body, buttons ]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 12;
    stack.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);
    [stack setCustomSpacing:18 afterView:body];

    NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 200)
                                              styleMask:NSWindowStyleMaskTitled
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    w.releasedWhenClosed = NO;
    w.title = @"just-desktops";
    w.contentView = stack;
    [w setContentSize:stack.fittingSize];
    [w center];
    [w makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    while (!AXIsProcessTrusted()) {
        @autoreleasepool {
            NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate dateWithTimeIntervalSinceNow:1]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
            if (e) [NSApp sendEvent:e];
        }
    }
    [w close];
    access_target = nil;
}

void app_set(const char *key, const char *value) {
    char path[1024];
    if (!config_path(path, sizeof path) || config_write_key(path, key, value) != 0) {
        alert(@"Could not save the setting", [NSString stringWithFormat:@"%s: %s", path, strerror(errno)]);
        return;
    }
    jd_restart();
}

void app_open_config(void) {
    char path[1024];
    if (!config_path(path, sizeof path) || config_ensure_file(path) != 0) {
        alert(@"Could not create the config file", [NSString stringWithFormat:@"%s: %s", path, strerror(errno)]);
        return;
    }
    NSURL *url = [NSURL fileURLWithPath:@(path)];
    if ([NSWorkspace.sharedWorkspace URLForApplicationToOpenURL:url]) {
        [NSWorkspace.sharedWorkspace openURL:url];
        return;
    }
    // A file without an extension often has no app of its own: open it as text, else show it in Finder.
    pid_t pid;
    char *argv[] = {"open", "-t", path, NULL};
    if (posix_spawn(&pid, "/usr/bin/open", NULL, NULL, argv, environ) != 0)
        [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ url ]];
}

static Class login_class(void) {
    static Class cls;
    if (!cls) {
        // Looked up when first needed, so the framework is not linked or loaded for the bare binary.
        dlopen("/System/Library/Frameworks/ServiceManagement.framework/ServiceManagement", RTLD_LAZY);
        cls = NSClassFromString(@"SMAppService");
    }
    return cls;
}

static SMAppService *login_service(void) { return [login_class() mainAppService]; }

int app_login_state(void) {
    switch (login_service().status) {
    case SMAppServiceStatusEnabled: return 1;
    case SMAppServiceStatusRequiresApproval: return 2;
    default: return 0;
    }
}

void app_toggle_login(void) {
    SMAppService *s = login_service();
    NSError *err = nil;
    if (s.status == SMAppServiceStatusEnabled || s.status == SMAppServiceStatusRequiresApproval) {
        if (![s unregisterAndReturnError:&err]) alert(@"Could not turn off Launch at Login", err.localizedDescription);
        return;
    }
    if (![s registerAndReturnError:&err]) {
        alert(@"Could not turn on Launch at Login", err.localizedDescription);
        return;
    }
    if (s.status == SMAppServiceStatusRequiresApproval) {
        alert(@"Allow just-desktops to open at login",
              @"Turn it on in System Settings > General > Login Items.");
        [login_class() openSystemSettingsLoginItems];
    }
}

void app_open_about(void) {
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"https://github.com/n-seiji/just-desktops"]];
}
