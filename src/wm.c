#include "wm.h"
#include "wm_priv.h"
#include "apps.h"
#include "ax.h"
#include "config.h"
#include "membership.h"
#include "menubar.h"
#include "shown.h"
#include "skylight.h"
#include <dispatch/dispatch.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// The apps and windows the manager knows, kept in step with Accessibility notifications. Workspaces
// are emulated like AeroSpace does: windows of inactive workspaces are parked at the bottom-right
// corner of the display furthest right, so macOS Spaces / SIP are never touched (see wm_park.c).

win_t *wins;
int nwins;
int cur_ws = 1;

static int capwins;
app_t *apps;
int napps;
static int capapps;
static AXUIElementRef sys;

// The desktops of the previous run (see membership.h), kept for windows that are tracked late.
#define RESTORED_S 30
static memb_win_t *restored;
static int nrestored;
static double restored_until;

static void ax_cb(AXObserverRef obs, AXUIElementRef el, CFStringRef n, void *ctx);

// ---- lookup ----

app_t *wm_find_app(pid_t pid) {
    for (int i = 0; i < napps; i++)
        if (apps[i].pid == pid) return &apps[i];
    return NULL;
}

win_t *wm_find_win(uint32_t wid) {
    if (!wid) return NULL;
    for (int i = 0; i < nwins; i++)
        if (wins[i].wid == wid) return &wins[i];
    return NULL;
}

// The tracked window with this element, or -1.
static int win_index(AXUIElementRef el) {
    for (int i = 0; i < nwins; i++)
        if (CFEqual(wins[i].el, el)) return i;
    return -1;
}

// Stops listening to an app's observer and releases it with the app's element.
static void drop_observer(AXObserverRef obs, AXUIElementRef el) {
    CFRunLoopRemoveSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(obs), kCFRunLoopDefaultMode);
    CFRelease(obs);
    CFRelease(el);
}

static void remove_win_at(int i) {
    CFRelease(wins[i].el);
    memmove(&wins[i], &wins[i + 1], sizeof *wins * (--nwins - i)); // keep order: cycling relies on it
}

// ---- membership across a restart ----

char *wm_membership(void) {
    size_t cap = 64 + 32 * (size_t)nwins;
    char *buf = malloc(cap);
    memb_win_t *t = malloc(sizeof *t * (nwins + 1));
    if (!buf || !t) {
        free(buf);
        free(t);
        return NULL;
    }
    int n = 0;
    for (int i = 0; i < nwins; i++)
        if (wins[i].ws != WS_EVERY) t[n++] = (memb_win_t){wins[i].pid, wins[i].wid, wins[i].ws};
    memb_format(buf, cap, time(NULL), cur_ws, t, n);
    free(t);
    return buf;
}

void wm_restore_membership(const char *text) {
    int cap = 1;
    for (const char *p = text; (p = strchr(p, '\n')); p++) cap++;
    memb_win_t *t = malloc(sizeof *t * cap);
    int cur, n = t ? memb_parse(text, time(NULL), config_workspaces(), &cur, t, cap) : -1;
    if (n < 0) {
        free(t);
        return;
    }
    cur_ws = cur;
    restored = t;
    nrestored = n;
    restored_until = CFAbsoluteTimeGetCurrent() + RESTORED_S;
}

static int restored_ws(pid_t pid, uint32_t wid) {
    if (restored && CFAbsoluteTimeGetCurrent() > restored_until) {
        free(restored);
        restored = NULL;
    }
    for (int i = 0; restored && i < nrestored; i++)
        if (restored[i].pid == pid && restored[i].wid == wid) {
            restored[i].wid = 0; // used once
            return restored[i].ws;
        }
    return 0;
}

// ---- tracking ----

static win_t *track(app_t *a, AXUIElementRef el) {
    uint32_t wid = 0;
    if (_AXUIElementGetWindow(el, &wid) != kAXErrorSuccess || !wid) return NULL;
    win_t *w = wm_find_win(wid);
    if (w) return w;
    if (!ax_is(el, kAXRoleAttribute, kAXWindowRole)) return NULL;
    if (!ax_is(el, kAXSubroleAttribute, kAXStandardWindowSubrole) &&
        !ax_is(el, kAXSubroleAttribute, kAXDialogSubrole))
        return NULL;

    if (nwins == capwins) {
        int cap = capwins ? capwins * 2 : 32;
        win_t *n = realloc(wins, sizeof *wins * cap);
        if (!n) return NULL;
        wins = n;
        capwins = cap;
    }
    w = &wins[nwins++];
    int back = restored_ws(a->pid, wid);
    *w = (win_t){.el = (AXUIElementRef)CFRetain(el), .pid = a->pid, .wid = wid,
                 .ws = memb_pick(a->assign, back, cur_ws), .minimized = ax_bool(el, kAXMinimizedAttribute)};
    wm_log_ws(w, -1, a->assign == 0 && back ? "restore-file" : "track");

    // A window left in the corner by a previous crash: bring it back.
    CGPoint p;
    CGRect m = CGDisplayBounds(CGMainDisplayID());
    // The main display's corner too: where a run before a display was plugged in, or an older version, parked.
    CGRect park = wm_park_display();
    if (ax_value(el, kAXPositionAttribute, kAXValueTypeCGPoint, &p) && (shown_is_parked(p, park) || shown_is_parked(p, m))) {
        CGPoint safe = shown_safe_point(m);
        // Only a window shown here is moved out. One of another desktop stays in the corner, parked, and
        // returns to the safe point when its desktop is shown.
        if (wm_here(w)) {
            ax_set_position(el, safe);
        } else {
            if (!shown_is_parked(p, park)) ax_set_position(el, shown_park_point(park));
            w->saved = safe;
            w->has_saved = true;
            w->hidden = true;
        }
        wm_log_recover(w);
    }

    void *ctx = (void *)(intptr_t)a->pid;
    AXObserverAddNotification(a->obs, el, kAXUIElementDestroyedNotification, ctx);
    AXObserverAddNotification(a->obs, el, kAXWindowMiniaturizedNotification, ctx);
    AXObserverAddNotification(a->obs, el, kAXWindowDeminiaturizedNotification, ctx);

    if (!wm_here(w)) wm_hide(w);
    return w;
}

win_t *wm_focused(void) {
    AXUIElementRef app = NULL, win = NULL;
    win_t *w = NULL;
    // The system-wide query fails for some apps (Slack: kAXErrorCannotComplete); ask the frontmost app itself.
    if (AXUIElementCopyAttributeValue(sys, kAXFocusedApplicationAttribute, (CFTypeRef *)&app) != kAXErrorSuccess || !app) {
        app_t *a = wm_find_app(apps_frontmost());
        if (!a) return NULL;
        app = (AXUIElementRef)CFRetain(a->el);
    }
    if (AXUIElementCopyAttributeValue(app, kAXFocusedWindowAttribute, (CFTypeRef *)&win) == kAXErrorSuccess && win) {
        uint32_t wid = 0;
        if (_AXUIElementGetWindow(win, &wid) == kAXErrorSuccess) w = wm_find_win(wid);
        if (!w) {
            pid_t pid = 0;
            AXUIElementGetPid(app, &pid);
            app_t *a = wm_find_app(pid);
            if (a) w = track(a, win);
        }
        CFRelease(win);
    }
    CFRelease(app);
    return w;
}

// ---- apps ----

// Tracks the app's windows; track returns at once for one it already has.
static void track_windows(app_t *a) {
    CFArrayRef list = NULL;
    if (AXUIElementCopyAttributeValue(a->el, kAXWindowsAttribute, (CFTypeRef *)&list) == kAXErrorSuccess && list) {
        for (CFIndex i = 0; i < CFArrayGetCount(list); i++) track(a, CFArrayGetValueAtIndex(list, i));
        CFRelease(list);
    }
}

static void add_app(pid_t pid, int assign, bool nopreview, int attempt) {
    if (wm_find_app(pid)) return;
    AXObserverRef obs = NULL;
    if (AXObserverCreate(pid, ax_cb, &obs) != kAXErrorSuccess) return;
    AXUIElementRef el = AXUIElementCreateApplication(pid);
    AXUIElementSetMessagingTimeout(el, 1.0);
    void *ctx = (void *)(intptr_t)pid;

    AXError err = AXObserverAddNotification(obs, el, kAXWindowCreatedNotification, ctx);
    if (err != kAXErrorSuccess && err != kAXErrorNotificationAlreadyRegistered) {
        CFRelease(obs);
        CFRelease(el);
        // Freshly launched apps are often not ready for AX yet.
        if (err == kAXErrorCannotComplete && attempt < 20)
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC), dispatch_get_main_queue(),
                           ^{ add_app(pid, assign, nopreview, attempt + 1); });
        return;
    }
    AXObserverAddNotification(obs, el, kAXFocusedWindowChangedNotification, ctx);
    AXObserverAddNotification(obs, el, kAXMainWindowChangedNotification, ctx);
    CFRunLoopAddSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(obs), kCFRunLoopDefaultMode);

    if (napps == capapps) {
        int cap = capapps ? capapps * 2 : 32;
        app_t *n = realloc(apps, sizeof *apps * cap);
        if (!n) {
            drop_observer(obs, el);
            return;
        }
        apps = n;
        capapps = cap;
    }
    apps[napps++] = (app_t){pid, el, obs, assign, nopreview};

    track_windows(&apps[napps - 1]);
    if (attempt > 0) {
        wm_set_trigger("retry");
        wm_on_focus();
    }
}

void wm_add_app(pid_t pid, const char *bundle_id) {
    if (wm_find_app(pid)) return; // apps_rescan offers every running app, and each redraw costs a focus query
    add_app(pid, config_assign(bundle_id), config_no_preview(bundle_id), 0);
    wm_ui_menubar_now();
}

// Under a lock the log shows no window tracked until its app was activated, so the first look at start can
// have found none. Called when the screen unlocks, once the apps that failed to register are added.
void wm_rescan(void) {
    wm_set_trigger("unlock");
    int before = nwins;
    for (int i = 0; i < napps; i++) track_windows(&apps[i]);
    wm_log_rescan(nwins - before);
    wm_ui_menubar_now();
    wm_settle(); // as at start: a parked window the scan finds focused is not a call for its desktop
    wm_on_focus();
}

void wm_remove_app(pid_t pid) {
    menubar_forget(pid);
    for (int i = nwins - 1; i >= 0; i--)
        if (wins[i].pid == pid) remove_win_at(i);
    for (int i = 0; i < napps; i++) {
        if (apps[i].pid != pid) continue;
        drop_observer(apps[i].obs, apps[i].el);
        apps[i] = apps[--napps];
        return;
    }
}

static void ax_cb(AXObserverRef obs, AXUIElementRef el, CFStringRef n, void *ctx) {
    (void)obs;
    app_t *a = wm_find_app((pid_t)(intptr_t)ctx);
    if (!a) return;
    if (CFEqual(n, kAXWindowCreatedNotification)) {
        wm_set_trigger("ax-created");
        track(a, el);
        wm_on_focus();
    } else if (CFEqual(n, kAXUIElementDestroyedNotification)) {
        wm_set_trigger("ax-destroyed");
        int i = win_index(el);
        if (i >= 0) {
            if (wm_here(&wins[i])) wm_settle();
            remove_win_at(i);
        }
        wm_on_focus();
    } else if (CFEqual(n, kAXFocusedWindowChangedNotification) || CFEqual(n, kAXMainWindowChangedNotification)) {
        wm_set_trigger("ax-focus");
        wm_on_focus();
    } else {
        bool mini = CFEqual(n, kAXWindowMiniaturizedNotification);
        int i = win_index(el);
        if (i >= 0) {
            wins[i].minimized = mini;
            if (mini && wm_here(&wins[i])) wm_settle();
        }
        wm_refresh_ui();
    }
}

void wm_init(void) {
    sys = AXUIElementCreateSystemWide();
    AXUIElementSetMessagingTimeout(sys, 1.0);
}

int wm_current(void) {
    return cur_ws;
}
