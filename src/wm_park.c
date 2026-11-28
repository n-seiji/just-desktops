#include "wm.h"
#include "wm_priv.h"
#include "apps.h"
#include "ax.h"
#include "shown.h"
#include <stdlib.h>

// Parking: a window of an inactive desktop is moved to the bottom-right corner of the display furthest right
// (see shown_park_display), and back to its place when its desktop is shown. wm_focus.c decides when.

static bool shutting_down; // wm_restore_all is running: no Accessibility call may wait long

// Some apps animate a move while AXEnhancedUserInterface is on (Chrome, once an assistive app such
// as a password manager turns it on), so a parked window would slide into the corner on screen.
// While quitting the toggle is skipped: it is three more calls to an app that may be hung, and a timed-out
// restore of the flag would leave it off.
static AXError move(win_t *w, CGPoint p) {
    app_t *a = wm_find_app(w->pid);
    bool eui = !shutting_down && a && ax_bool(a->el, CFSTR("AXEnhancedUserInterface"));
    if (eui) AXUIElementSetAttributeValue(a->el, CFSTR("AXEnhancedUserInterface"), kCFBooleanFalse);
    AXError err = ax_set_position(w->el, p);
    if (eui) AXUIElementSetAttributeValue(a->el, CFSTR("AXEnhancedUserInterface"), kCFBooleanTrue);
    return err;
}

// A window already in the corner is not moved again, and never becomes its own place to return to.
void wm_hide(win_t *w) {
    if (w->hidden) return;
    CGPoint p;
    if (!ax_value(w->el, kAXPositionAttribute, kAXValueTypeCGPoint, &p)) return;
    CGRect b = wm_park_display();
    w->saved = shown_return_point(p, b, CGDisplayBounds(CGMainDisplayID()), w->has_saved, w->saved, CFAbsoluteTimeGetCurrent() - w->shown_at);
    w->has_saved = true;
    if (!shown_is_parked(p, b)) {
        if (move(w, shown_park_point(b)) == kAXErrorSuccess) w->parked_at = CFAbsoluteTimeGetCurrent();
    }
    w->hidden = true;
}

// macOS and apps adjust a position they are asked for (the park point ends up 41pt higher, or more), and may not
// have applied it yet: read it back once and ask again if it is elsewhere. Not while quitting, with its
// short budget.
static void recheck_position(win_t *w) {
    CGPoint got;
    if (shutting_down || !ax_value(w->el, kAXPositionAttribute, kAXValueTypeCGPoint, &got)) return;
    if (!shown_missed(w->saved, got)) return;
    wm_log_reshow(w, w->saved, got);
    move(w, w->saved);
}

// A window whose move failed counts as shown all the same: a busy app may still apply it late, and it must
// then be parked again. One that stayed in the corner keeps its place the next time it is parked (wm_hide).
AXError wm_show(win_t *w) {
    if (!w->hidden) return kAXErrorSuccess;
    AXError err = move(w, w->saved);
    if (err == kAXErrorSuccess) recheck_position(w);
    w->hidden = false;
    w->shown_at = CFAbsoluteTimeGetCurrent();
    return err;
}

// The online displays, not the active ones: a display that is asleep is still where it was, and a window
// parked at a corner it touches would be in view when it wakes.
CGRect wm_park_display(void) {
    CGDirectDisplayID ds[8];
    CGRect r[8];
    uint32_t n = 0;
    CGGetOnlineDisplayList(8, ds, &n);
    for (uint32_t k = 0; k < n; k++) r[k] = CGDisplayBounds(ds[k]);
    return n ? shown_park_display(r, n) : CGDisplayBounds(CGMainDisplayID());
}

bool wm_in_place(double since) {
    const void **ids = malloc(sizeof *ids * (nwins ? nwins : 1));
    if (!ids) return true;
    CFIndex n = 0;
    for (int i = 0; i < nwins; i++) {
        const win_t *w = &wins[i];
        bool came = wm_here(w) && !w->hidden && w->shown_at >= since;
        bool went = w->hidden && w->parked_at >= since;
        // The array holds the ids themselves, as pointer-sized values with no callbacks.
        if ((came || went) && !w->minimized && !apps_hidden(w->pid)) ids[n++] = (const void *)(uintptr_t)w->wid;
    }
    CFArrayRef want = n ? CFArrayCreate(kCFAllocatorDefault, ids, n, NULL) : NULL;
    free(ids);
    CFArrayRef info = want ? CGWindowListCreateDescriptionFromArray(want) : NULL;
    if (want) CFRelease(want);
    if (!info) return true;
    CGRect park = wm_park_display();
    bool ok = true;
    for (CFIndex i = 0; ok && i < CFArrayGetCount(info); i++) {
        CFDictionaryRef d = CFArrayGetValueAtIndex(info, i);
        CFNumberRef num = CFDictionaryGetValue(d, kCGWindowNumber);
        CFDictionaryRef b = CFDictionaryGetValue(d, kCGWindowBounds);
        CGRect r;
        int64_t id = 0;
        if (!num || !b || !CFNumberGetValue(num, kCFNumberSInt64Type, &id) || !CGRectMakeWithDictionaryRepresentation(b, &r)) continue;
        const win_t *w = wm_find_win((uint32_t)id);
        if (w) ok = shown_is_parked(r.origin, park) == w->hidden;
    }
    CFRelease(info);
    return ok;
}

// The app of window i did not answer: its later windows are skipped, or each would wait out its own timeout.
static void skip_rest_of_app(bool *hung, int i) {
    for (int j = i + 1; j < nwins; j++)
        if (wins[j].pid == wins[i].pid) hung[j] = true;
}

// The displays changed, so the park corner may have moved: parked windows go to the new one. Their
// places to return to are kept.
// It also runs when nothing moved (a wake, another display's resolution), so a window still in the corner is
// left alone, and an app that timed out is not asked again: each call can block for a second.
void wm_displays_changed(void) {
    if (shutting_down) return;
    CGRect d = wm_park_display();
    int n = 0;
    bool *hung = calloc(nwins + 1, 1);
    for (int i = 0; i < nwins; i++) {
        win_t *w = &wins[i];
        CGPoint p;
        if (!w->hidden || (hung && hung[i])) continue;
        if (ax_value(w->el, kAXPositionAttribute, kAXValueTypeCGPoint, &p) && shown_is_parked(p, d)) continue;
        AXError err = move(w, shown_park_point(d));
        if (err == kAXErrorSuccess) n++;
        if (err == kAXErrorCannotComplete && hung) skip_rest_of_app(hung, i);
    }
    free(hung);
    if (n) wm_log_repark(n);
}

// The way out: a hung app must not hold up quitting or logging out, so the whole walk has a deadline
// and every Accessibility call a short timeout.
#define RESTORE_S 2.5
#define RESTORE_AX_S 0.25

void wm_restore_all(void) {
    shutting_down = true;
    wm_hold_drop();
    double end = CFAbsoluteTimeGetCurrent() + RESTORE_S;
    bool *hung = calloc(nwins + 1, 1); // per window: its app already timed out, so the rest of its windows are left
    for (int i = 0; i < nwins; i++) {
        win_t *w = &wins[i];
        if (!w->hidden || (hung && hung[i])) continue;
        if (CFAbsoluteTimeGetCurrent() >= end) break;
        if (!apps_alive(w->pid)) continue;
        app_t *a = wm_find_app(w->pid);
        if (a) AXUIElementSetMessagingTimeout(a->el, RESTORE_AX_S);
        AXUIElementSetMessagingTimeout(w->el, RESTORE_AX_S);
        if (wm_show(w) == kAXErrorCannotComplete && hung) skip_rest_of_app(hung, i);
    }
    free(hung);
}
