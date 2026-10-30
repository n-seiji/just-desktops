#include "border.h"
#include "appearance.h"
#include "border_geom.h"
#include "skylight.h"
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <math.h>

// A thin ring drawn directly through SkyLight (no AppKit), floating
// off the focused window with a small gap and ordered right above it. It is
// four thin windows (top, bottom, left, right) instead of one window the size
// of the target: a window's backing store is its full bounding rect, so one
// big window costs tens of MB even when it is mostly transparent.

#define RIM_FADE 0.18 // fraction of the ring's height the easing takes

// The ring is white in Dark mode and black in Light mode; its alpha is strongest
// at the very top (rim) and eases to ring over RIM_FADE.
typedef struct {
    CGFloat gray, rim, ring;
} palette_t;
static const palette_t DARK = {1, 0.9, 0.6}, LIGHT = {0, 0.75, 0.55};

// WindowServer event numbers (the same ones JankyBorders and yabai register for).
enum { EVENT_WINDOW_MOVE = 806, EVENT_WINDOW_RESIZE = 807 };

static struct {
    uint32_t wid;
    CGContextRef ctx;
} edges[EDGE_COUNT];

static int cid;
static bool shown;
static uint32_t last_target;
static CGRect last_outer; // the ring's rect, not clipped to the display
static CGRect last_clip;
static const palette_t *pal = &DARK;
static CGFloat radius = DEFAULT_RADIUS; // the followed window's corner radius

// The corner radius WindowServer applies to the window; it is 16 for most, 20 to 26 for newer styles.
static CGFloat window_radius(uint32_t wid) {
    CGFloat r = DEFAULT_RADIUS;
    static corner_radii_fn get_radii;
    static bool looked_up;
    if (!looked_up) {
        get_radii = (corner_radii_fn)dlsym(RTLD_DEFAULT, "SLSWindowIteratorGetCornerRadii");
        looked_up = true;
    }
    if (!get_radii) return r;
    int n = (int)wid;
    CFNumberRef num = CFNumberCreate(NULL, kCFNumberIntType, &n);
    CFArrayRef ids = CFArrayCreate(NULL, (const void **)&num, 1, &kCFTypeArrayCallBacks);
    CFTypeRef query = SLSWindowQueryWindows(cid, ids, 1);
    CFTypeRef it = query ? SLSWindowQueryResultCopyWindows(query) : NULL;
    if (it && SLSWindowIteratorAdvance(it)) {
        CFArrayRef radii = get_radii(it);
        CGFloat v;
        if (radii && CFArrayGetCount(radii) > 0 && CFNumberGetValue(CFArrayGetValueAtIndex(radii, 0), kCFNumberCGFloatType, &v))
            r = fmin(fmax(v, 0), 64);
    }
    if (it) CFRelease(it);
    if (query) CFRelease(query);
    CFRelease(ids);
    CFRelease(num);
    return r;
}

static void appearance_changed(CFNotificationCenterRef, void *, CFNotificationName, const void *, CFDictionaryRef);
static void window_changed(uint32_t event, uint32_t *wid, size_t len, void *ctx);
static void drain_events(CFMachPortRef port, void *msg, CFIndex size, void *info);

void border_init(void) {
    cid = SLSMainConnectionID();
    pal = appearance_dark() ? &DARK : &LIGHT;
    CFNotificationCenterAddObserver(CFNotificationCenterGetDistributedCenter(), NULL, appearance_changed,
                                    CFSTR("AppleInterfaceThemeChangedNotification"), NULL,
                                    CFNotificationSuspensionBehaviorDeliverImmediately);
    SLSRegisterNotifyProc(window_changed, EVENT_WINDOW_MOVE, NULL);
    SLSRegisterNotifyProc(window_changed, EVENT_WINDOW_RESIZE, NULL);
    // The notify procs run while the connection's event queue is drained.
    mach_port_t port;
    if (SLSGetEventPort(cid, &port) == kCGErrorSuccess) {
        CFMachPortRef cf_port = CFMachPortCreateWithPort(NULL, port, drain_events, NULL, false);
        _CFMachPortSetOptions(cf_port, 0x40);
        CFRunLoopSourceRef src = CFMachPortCreateRunLoopSource(NULL, cf_port, 0);
        CFRunLoopAddSource(CFRunLoopGetMain(), src, kCFRunLoopDefaultMode);
        CFRelease(src);
        CFRelease(cf_port);
    }
    for (int e = 0; e < EDGE_COUNT; e++) {
        CGRect r = {{0, 0}, {1, 1}};
        CFTypeRef region = NULL;
        CGSNewRegionWithRect(&r, &region);
        if (!region) return;
        SLSNewWindow(cid, 2, 0, 0, region, &edges[e].wid);
        CFRelease(region);
        uint32_t wid = edges[e].wid;
        if (!wid) return;

        uint64_t tags[2] = {(1ULL << 1) | (1ULL << 9), 0}; // floating, ignores mouse events
        SLSSetWindowTags(cid, wid, tags, 64);
        SLSSetWindowResolution(cid, wid, 2.0);
        SLSSetWindowOpacity(cid, wid, false);
        edges[e].ctx = SLWindowContextCreate(cid, wid, NULL);
        if (edges[e].ctx) CGContextSetInterpolationQuality(edges[e].ctx, kCGInterpolationNone);
    }
}

static void draw_edge(int e, CGRect outer, CGRect clip) {
    CGRect er = border_edge_rect(e, outer, radius, clip);
    bool off_display = CGRectIsEmpty(er);
    if (off_display) er = (CGRect){outer.origin, {1, 1}}; // a cleared 1x1 keeps the window in the ordering
    CGRect local = {{0, 0}, er.size};
    CFTypeRef region = NULL;
    CGSNewRegionWithRect(&local, &region);
    if (!region) return;
    SLSSetWindowShape(cid, edges[e].wid, 0, 0, region);
    CFRelease(region);
    SLSMoveWindow(cid, edges[e].wid, &er.origin);

    CGContextRef ctx = edges[e].ctx;
    CGContextClearRect(ctx, local);
    if (off_display) {
        CGContextFlush(ctx);
        return;
    }
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, 0, er.size.height); // SkyLight contexts are y-up, frames are y-down
    CGContextScaleCTM(ctx, 1, -1);
    CGRect path_rect = CGRectInset((CGRect){{0, 0}, outer.size}, RING_WIDTH / 2, RING_WIDTH / 2);
    CGFloat ring_r = border_ring_radius(radius, outer.size);
    CGFloat ox = outer.origin.x - er.origin.x, oy = outer.origin.y - er.origin.y;
    CGAffineTransform to_local = CGAffineTransformMakeTranslation(ox, oy);
    CGPathRef path = CGPathCreateWithRoundedRect(path_rect, ring_r, ring_r, &to_local);
    CGContextAddPath(ctx, path);
    CGContextSetLineWidth(ctx, RING_WIDTH);
    CGContextReplacePathWithStrokedPath(ctx);
    CGContextClip(ctx);
    CGPathRelease(path);

    // One vertical gradient in the ring's own coordinates, so every edge window
    // draws its slice of the same colours and there is no seam between them.
    enum { STOPS = 5 };
    CGFloat comps[STOPS * 4], locs[STOPS];
    for (int i = 0; i < STOPS; i++) {
        CGFloat u = (CGFloat)i / (STOPS - 1);
        CGFloat ease = u * u * (3 - 2 * u); // smoothstep
        locs[i] = u;
        comps[i * 4] = comps[i * 4 + 1] = comps[i * 4 + 2] = pal->gray;
        comps[i * 4 + 3] = pal->rim + (pal->ring - pal->rim) * ease;
    }
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGGradientRef grad = CGGradientCreateWithColorComponents(space, comps, locs, STOPS);
    CGContextDrawLinearGradient(ctx, grad, (CGPoint){0, oy}, (CGPoint){0, oy + RIM_FADE * outer.size.height},
                                kCGGradientDrawsAfterEndLocation);
    CGGradientRelease(grad);
    CGColorSpaceRelease(space);
    CGContextRestoreGState(ctx);
    CGContextFlush(ctx);
}

// Redraws the shown ring in the new appearance.
static void appearance_changed(CFNotificationCenterRef center __unused, void *observer __unused,
                               CFNotificationName name __unused, const void *object __unused,
                               CFDictionaryRef info __unused) {
    pal = appearance_dark() ? &DARK : &LIGHT;
    if (!shown) return;
    SLSDisableUpdate(cid);
    for (int e = 0; e < EDGE_COUNT; e++) draw_edge(e, last_outer, last_clip);
    SLSReenableUpdate(cid);
}

// WindowServer reports 0 for a window that just opened; the real radius arrives within about a second.
#define RADIUS_RETRY_MS 100
#define RADIUS_RETRIES 20
static unsigned radius_gen;

static void recheck_radius(uint32_t wid, unsigned gen, int left) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, RADIUS_RETRY_MS * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        if (gen != radius_gen || !shown || last_target != wid) return;
        CGFloat r = window_radius(wid);
        if (r == 0) {
            if (left > 1) recheck_radius(wid, gen, left - 1);
            return;
        }
        radius = r;
        if (border_too_small(last_outer.size, radius)) {
            border_hide();
            return;
        }
        SLSDisableUpdate(cid);
        for (int e = 0; e < EDGE_COUNT; e++) draw_edge(e, last_outer, last_clip);
        SLSReenableUpdate(cid);
    });
}

void border_show(CGRect frame, uint32_t target_wid) {
    if (!edges[EDGE_COUNT - 1].ctx) return;
    CGRect outer = CGRectInset(frame, -RING_OUTSET, -RING_OUTSET);
    CGRect clip = CGRectInfinite; // only the edge windows are cut to the display; the ring is laid out in full
    CGDirectDisplayID d;
    uint32_t n = 0;
    if (CGGetDisplaysWithRect(frame, 1, &d, &n) == kCGErrorSuccess && n) clip = CGDisplayBounds(d);
    bool same = shown && target_wid == last_target;
    bool resized = !same || !CGSizeEqualToSize(outer.size, last_outer.size);
    for (int e = 0; e < EDGE_COUNT && !resized; e++) {
        CGRect a = border_edge_rect(e, outer, radius, clip), b = border_edge_rect(e, last_outer, radius, last_clip);
        // A clipped edge can keep its size while the ring moves inside it, so its offset counts too.
        resized = !CGSizeEqualToSize(a.size, b.size) || a.origin.x - outer.origin.x != b.origin.x - last_outer.origin.x ||
                  a.origin.y - outer.origin.y != b.origin.y - last_outer.origin.y;
    }
    if (!same) {
        radius = window_radius(target_wid);
        unsigned gen = ++radius_gen;
        if (radius == 0) recheck_radius(target_wid, gen, RADIUS_RETRIES);
    }
    if (border_too_small(outer.size, radius)) {
        border_hide();
        return;
    }

    if (same && CGRectEqualToRect(outer, last_outer) && CGRectEqualToRect(clip, last_clip)) return;

    SLSDisableUpdate(cid);
    if (!resized) { // dragged: only the position changed
        for (int e = 0; e < EDGE_COUNT; e++) {
            CGRect er = border_edge_rect(e, outer, radius, clip);
            if (!CGRectIsEmpty(er)) SLSMoveWindow(cid, edges[e].wid, &er.origin);
        }
    } else {
        for (int e = 0; e < EDGE_COUNT; e++) draw_edge(e, outer, clip);
    }
    if (!same) {
        SLSRequestNotificationsForWindows(cid, &target_wid, 1); // move / resize events for this window only
        int level = 0;
        SLSGetWindowLevel(cid, target_wid, &level);
        for (int e = 0; e < EDGE_COUNT; e++) {
            SLSSetWindowLevel(cid, edges[e].wid, level);
            SLSOrderWindow(cid, edges[e].wid, 1, target_wid); // 1 = above
        }
    }
    SLSReenableUpdate(cid);
    shown = true;
    last_target = target_wid;
    last_outer = outer;
    last_clip = clip;
}

void border_hide(void) {
    if (!shown) return;
    for (int e = 0; e < EDGE_COUNT; e++) SLSOrderWindow(cid, edges[e].wid, 0, 0); // 0 = out
    shown = false;
}

static void drain_events(CFMachPortRef port __unused, void *msg __unused, CFIndex size __unused, void *info __unused) {
    CGEventRef ev;
    while ((ev = SLEventCreateNextEvent(cid))) CFRelease(ev);
}

// WindowServer reports the followed window's moves and resizes as they happen, ahead of
// the Accessibility notifications, which arrive late and coalesced.
static void window_changed(uint32_t event __unused, uint32_t *wid, size_t len, void *ctx __unused) {
    if (!shown || len < sizeof *wid || *wid != last_target) return;
    CGRect frame;
    if (SLSGetWindowBounds(cid, *wid, &frame) != kCGErrorSuccess) return;
    border_show(frame, *wid);
}
