#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include "thumb.h"

// Window thumbnails for the square switcher. ScreenCaptureKit is linked weakly and nothing here
// runs unless the square style is configured, so the list and off styles pay nothing for it.
// The shareable window list and the pending requests live only between thumb_begin and thumb_end.

#define MAX_PX_W 1152.0 // the thumbnail box, 576x330 pt, at 2x
#define MAX_PX_H 660.0

static unsigned generation;
static id content;                // SCShareableContent, once it has arrived
static NSMutableArray *pending;   // @[@(wid), done block] pairs waiting for content

void thumb_request_access(void) {
    if (@available(macOS 14.0, *)) { // earlier systems cannot take the screenshots, so the prompt would be pointless
        if (!CGPreflightScreenCaptureAccess()) CGRequestScreenCaptureAccess();
    }
}

bool thumb_ready(void) {
    if (!CGPreflightScreenCaptureAccess()) return false;
    if (@available(macOS 14.0, *)) return NSClassFromString(@"SCScreenshotManager") != nil;
    return false;
}

static void capture(uint32_t wid, void (^done)(CGImageRef)) API_AVAILABLE(macos(14.0)) {
    SCWindow *win = nil;
    for (SCWindow *w in ((SCShareableContent *)content).windows) {
        if (w.windowID == wid) {
            win = w;
            break;
        }
    }
    CGSize fr = win.frame.size;
    if (!win || fr.width < 1 || fr.height < 1) {
        done(NULL);
        return;
    }
    CGFloat s = fmin(fmin(MAX_PX_W / fr.width, MAX_PX_H / fr.height), 2);
    SCStreamConfiguration *cfg = [SCStreamConfiguration new];
    cfg.width = (size_t)lround(fr.width * s);
    cfg.height = (size_t)lround(fr.height * s);
    cfg.showsCursor = NO;
    cfg.ignoreShadowsSingleWindow = YES;
    SCContentFilter *filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:win];
    unsigned gen = generation;
    [SCScreenshotManager captureImageWithFilter:filter configuration:cfg completionHandler:^(CGImageRef img, NSError *err) {
        (void)err;
        CGImageRef keep = img ? CGImageRetain(img) : NULL;
        dispatch_async(dispatch_get_main_queue(), ^{
            if (gen == generation) done(keep);
            if (keep) CGImageRelease(keep);
        });
    }];
}

void thumb_begin(void) {
    generation++;
    content = nil;
    pending = [NSMutableArray new];
    if (@available(macOS 14.0, *)) {
        unsigned gen = generation;
        [SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:YES
                                                     completionHandler:^(SCShareableContent *c, NSError *err) {
            (void)err;
            dispatch_async(dispatch_get_main_queue(), ^{
                if (gen != generation) return;
                content = c;
                NSArray *queued = pending;
                pending = nil;
                for (NSArray *req in queued) capture((uint32_t)[req[0] unsignedIntValue], req[1]);
            });
        }];
    }
}

void thumb_capture(uint32_t wid, void (^done)(CGImageRef img)) {
    if (@available(macOS 14.0, *)) {
        if (content) {
            capture(wid, done);
            return;
        }
        if (pending) {
            [pending addObject:@[@(wid), [done copy]]];
            return;
        }
    }
    done(NULL);
}

void thumb_end(void) {
    generation++;
    content = nil;
    pending = nil;
}
