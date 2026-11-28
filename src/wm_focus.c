#include "wm.h"
#include "wm_priv.h"
#include "apps.h"
#include "ax.h"
#include "border.h"
#include "config.h"
#include "follow.h"
#include "hist.h"
#include "hotkey.h"
#include "menubar.h"
#include "shown.h"
#include "skylight.h"
#include <dispatch/dispatch.h>
#include <os/log.h>
#include <stdio.h>

// Parking, switching and focus. A window of an inactive desktop sits at the bottom-right corner of
// the display furthest right (see shown_park_display); a desktop switch parks one set and unparks another with
// the screen held still until the new ones are in place (see release_when_in_place), and a parked window that
// gets focused (Raycast, Cmd-Tab, Dock) switches to its desktop instead of coming here, after a short
// check that the user did not just minimize, close or quit their window here. In the quiet period after a
// switch that focus is macOS's doing and is ignored, unless a click, Return / Enter or a shortcut came
// just before it: that is a call, followed as usual.

static uint32_t last_focus[JD_MAX_WS + 1];
static uint32_t stale; // parked window macOS still reports as focused
static double settle_until; // until then, parked windows macOS activates on its own are not followed
static double settle_start; // when the current quiet period began: extensions stop SETTLE_MAX_S after it
static hist_t hist = {.at = -1};
static double hold_until; // until then, only the window wm_history went to is recorded; 0 when recording
static bool hold_switcher; // ⌥Tab is stepping through windows: they are not recorded
static const char *trigger = "start"; // what led to the current wm_on_focus, for the log

// After a desktop change macOS hands activation from one parked window's app to the next,
// about a second apart; each one extends the quiet period (to SETTLE_MAX_S at most) in which a call for
// another desktop is ignored.
#define SETTLE_S 1.5
#define SETTLE_MAX_S 3.0

void wm_settle(void) {
    settle_start = CFAbsoluteTimeGetCurrent();
    settle_until = settle_start + SETTLE_S;
}

// Closing, minimizing or quitting makes macOS focus some other window: not a call for its desktop.
void wm_settle_pid(pid_t pid) {
    for (int i = 0; i < nwins; i++)
        if (wins[i].pid == pid && wm_here(&wins[i])) {
            wm_settle();
            return;
        }
}

// ---- hide / show / focus ----

// False when the app did not answer: it is hung, and the focus stays where it was. Other errors are not
// counted: some apps report one for a window they focus all the same.
static bool focus(win_t *w) {
    app_t *a = wm_find_app(w->pid);
    AXError front = a ? AXUIElementSetAttributeValue(a->el, kAXFrontmostAttribute, kCFBooleanTrue) : kAXErrorSuccess;
    AXError main = AXUIElementSetAttributeValue(w->el, kAXMainAttribute, kCFBooleanTrue);
    AXError raise = AXUIElementPerformAction(w->el, kAXRaiseAction);
    return front != kAXErrorCannotComplete && main != kAXErrorCannotComplete && raise != kAXErrorCannotComplete;
}

// A window that can be given focus: a minimized one cannot.
static win_t *first_in(int ws) {
    for (int i = 0; i < nwins; i++)
        if (wins[i].ws == ws && !wins[i].minimized) return &wins[i];
    return NULL;
}

// *took (may be NULL): the window returned was given the focus, see focus.
static win_t *focus_in_current(bool *took) {
    win_t *t = wm_find_win(last_focus[cur_ws]);
    if (!t || t->ws != cur_ws || t->minimized) t = first_in(cur_ws);
    bool ok = t && focus(t);
    if (took) *took = ok;
    return t;
}

// ---- log ----

// `log show --predicate 'subsystem == "dev.n-seiji.just-desktops"'`; app names and reasons are public,
// window titles never appear.
static void app_name(pid_t pid, char *buf, size_t cap) {
    CFStringRef s = apps_name(pid);
    if (!s || !CFStringGetCString(s, buf, cap, kCFStringEncodingUTF8)) snprintf(buf, cap, "?");
    if (s) CFRelease(s);
}

static os_log_t ws_log(void) {
    static os_log_t l;
    if (!l) l = os_log_create("dev.n-seiji.just-desktops", "ws");
    return l;
}

// What a line says about w besides its desktop: its app, other windows of that app here, ms since input.
static void log_context(const win_t *w, char *app, size_t cap, int *here, long long *idle) {
    app_name(w->pid, app, cap);
    *here = 0;
    for (int i = 0; i < nwins; i++)
        if (&wins[i] != w && wins[i].pid == w->pid && wm_here(&wins[i])) (*here)++;
    double idle_s = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateCombinedSessionState, kCGAnyInputEventType);
    *idle = (long long)((idle_s < 1e9 ? idle_s : 1e9) * 1000);
}

void wm_log_ws(const win_t *w, int old, const char *reason) {
    char app[128];
    int here;
    long long idle;
    log_context(w, app, sizeof app, &here, &idle);
    os_log(ws_log(), "wid=%u app=%{public}s ws=%d->%d reason=%{public}s trigger=%{public}s cur=%d settling=%d stale=%d same_app_here=%d idle_ms=%lld",
           w->wid, app, old, w->ws, reason, trigger, cur_ws, CFAbsoluteTimeGetCurrent() < settle_until, w->wid == stale, here, idle);
}

void wm_log_recover(const win_t *w) {
    char app[128];
    app_name(w->pid, app, sizeof app);
    os_log(ws_log(), "wid=%u app=%{public}s found parked by a previous run", w->wid, app);
}

void wm_log_rescan(int n) { os_log(ws_log(), "reason=rescan trigger=unlock n=%d", n); }

void wm_log_repark(int n) { os_log(ws_log(), "reason=repark n=%d", n); }

// A parked window that was focused: the desktop it belongs to is unchanged, cur goes from cur_ws to `to`
// (a refocus stays on cur_ws and prints it once).
static void log_focus(const win_t *w, const char *reason, int to) {
    char app[128];
    int here;
    long long idle;
    log_context(w, app, sizeof app, &here, &idle);
    bool settling = CFAbsoluteTimeGetCurrent() < settle_until;
    char cur[24];
    if (to == cur_ws) snprintf(cur, sizeof cur, "%d", cur_ws);
    else snprintf(cur, sizeof cur, "%d->%d", cur_ws, to);
    os_log(ws_log(), "wid=%u app=%{public}s ws=%d reason=%{public}s trigger=%{public}s cur=%{public}s settling=%d stale=%d same_app_here=%d idle_ms=%lld",
           w->wid, app, w->ws, reason, trigger, cur, settling, w->wid == stale, here, idle);
}

void wm_log_reshow(const win_t *w, CGPoint want, CGPoint got) {
    char app[128];
    app_name(w->pid, app, sizeof app);
    // parked=1: still in the corner, so the first move had not been applied; 0: it was put somewhere else.
    os_log(ws_log(), "wid=%u app=%{public}s reason=reshow want=%d,%d found=%d,%d parked=%d", w->wid, app,
           (int)want.x, (int)want.y, (int)got.x, (int)got.y, shown_is_parked(got, wm_park_display()));
}

void wm_set_trigger(const char *what) { trigger = what; }

static void log_hide(pid_t pid) {
    char app[128];
    app_name(pid, app, sizeof app);
    os_log(ws_log(), "app=%{public}s reason=hide trigger=%{public}s cur=%d", app, trigger, cur_ws);
}

// ---- ui ----

static uint32_t ring_wid; // the window the ring is on, 0 when it is hidden

// A panel of an app we do not track has focus (Raycast, Spotlight: accessory apps), floating over the windows here.
static bool panel_focused(void) {
    pid_t pid = apps_frontmost();
    return pid > 0 && !wm_find_app(pid);
}

// w is the focused window, looked up once by the caller: each lookup is a round trip to the app.
// While a panel has focus the ring stays on its window, unless that window is gone, parked or minimized.
static void refresh_border(win_t *w) {
    if (!config_border()) return;
    if (!w && panel_focused()) w = wm_find_win(ring_wid);
    CGPoint p;
    CGSize s;
    if (!w || w->hidden || !wm_here(w) || apps_hidden(w->pid) ||
        w->minimized || ax_bool(w->el, CFSTR("AXFullScreen")) ||
        !ax_value(w->el, kAXPositionAttribute, kAXValueTypeCGPoint, &p) ||
        !ax_value(w->el, kAXSizeAttribute, kAXValueTypeCGSize, &s)) {
        border_hide();
        ring_wid = 0;
        return;
    }
    border_show((CGRect){p, s}, w->wid);
    ring_wid = w->wid;
}

void wm_refresh_ui(void) {
    if (!config_border() && !menubar_ready()) return;
    wm_ui_draw(wm_focused());
}

// The ring and the menu bar for w, which the caller looked up once: see refresh_border.
void wm_ui_draw(win_t *w) {
    refresh_border(w);
    wm_refresh_menubar(w);
}

// The menu bar for the focused window. Not asked before it is up: the focus query is a round trip.
void wm_ui_menubar_now(void) {
    if (menubar_ready()) wm_refresh_menubar(wm_focused());
}

// ---- focus and desktop changes ----

// While settling, a parked window that takes focus is left alone, so nothing would look at it again once the
// quiet period ends. One check per quiet period is queued for then; only the newest one runs.
static unsigned recheck_gen;
static void on_focus(bool recheck);

static void recheck_after_settle(void) {
    unsigned gen = ++recheck_gen;
    double wait = settle_until - CFAbsoluteTimeGetCurrent() + 0.02;
    run_after(wait > 0 ? wait : 0, ^{
        if (gen != recheck_gen) return;
        trigger = "recheck";
        on_focus(true);
    });
}

// The focused window is on this desktop: remember it, and put it in the history unless a ⌥[ or ⌥Tab is under way.
static void visit(uint32_t wid) {
    if (hold_switcher) return;
    bool target = hist.at >= 0 && wid == hist.wid[hist.at];
    if (target || CFAbsoluteTimeGetCurrent() >= hold_until) {
        hold_until = 0;
        hist_visit(&hist, wid);
    }
}

// A call for a parked window is acted on after FOLLOW_DELAY_S, not at once. macOS focuses the app's other
// window about 0.7 s before it reports that the focused one was minimized (closing and quitting are likely
// the same), so at the notice the cause is not yet known. Only the newest call runs: any later focus
// event or desktop switch cancels it. A fresh shortcut (see quick_call) skips the wait.
#define FOLLOW_DELAY_S 0.3
static unsigned follow_gen;

// The window last used on this desktop is going: its minimize, close or quit is under way but macOS has not
// told us yet. One already handled (minimized flag set, or untracked) is not, or a stale last_focus would
// swallow every later call. The read gets a short timeout: a quitting app may not answer, and counts as left.
#define LEFT_AX_S 0.25
static bool cur_window_left(void) {
    uint32_t wid = last_focus[cur_ws];
    if (!wid) return false;
    win_t *l = wm_find_win(wid);
    bool tracked_here = l && wm_here(l);
    bool alive = tracked_here && apps_alive(l->pid);
    ax_read_t read = READ_OTHER;
    bool minimized = false;
    if (alive && !l->minimized) {
        CFTypeRef v = NULL;
        AXUIElementSetMessagingTimeout(l->el, LEFT_AX_S);
        AXError err = AXUIElementCopyAttributeValue(l->el, kAXMinimizedAttribute, &v);
        AXUIElementSetMessagingTimeout(l->el, 0);
        read = err == kAXErrorSuccess ? READ_OK : err == kAXErrorInvalidUIElement ? READ_INVALID
             : err == kAXErrorCannotComplete ? READ_BUSY : READ_OTHER;
        minimized = v && CFGetTypeID(v) == CFBooleanGetTypeID() && CFBooleanGetValue(v);
        if (v) CFRelease(v);
    }
    return cur_left(tracked_here, tracked_here && l->minimized, alive, read, minimized);
}

// A check queued earlier is not cancelled by wm_focus_wid, which leaves follow_gen alone, nor by wm_move_focused
// unless it moves the window to the desktop shown now.
// It is safe because follow_check_now re-reads the focused window and the quiet period when it runs.
// user_call: the call came from input during the quiet period (see on_focus): the quiet period does not hold it back.
static void follow_check_now(uint32_t wid, const char *why, bool user_call) {
    win_t *f = wm_focused();
    bool still = f && f->wid == wid;
    bool here = still && wm_here(f);
    bool settling = CFAbsoluteTimeGetCurrent() < settle_until;
    switch (follow_check(still, here, settling, still && !here && !settling && cur_window_left(), user_call)) {
    case FOLLOW_LEFT:
        // The user left this desktop's window: not a call. Let the focus return here, or stay if it is empty.
        trigger = why;
        log_focus(f, "left", cur_ws);
        last_focus[cur_ws] = 0;
        wm_settle();
        recheck_after_settle();
        return;
    case FOLLOW_SWITCH:
        trigger = why;
        // wm_switch can track new windows and move wins: f is not used after it.
        last_focus[f->ws] = f->wid;
        log_focus(f, user_call ? "call" : "follow", f->ws);
        visit(f->wid);
        wm_switch(f->ws);
        return;
    case FOLLOW_WAIT:
        // A close, minimize or quit noticed meanwhile started a quiet period: the focus comes back here after it.
        trigger = why;
        log_focus(f, "dropped", cur_ws);
        recheck_after_settle();
        return;
    default:
        return;
    }
}

static double last_click_ago(void);

// A window clicked in Mission Control is a call with no doubt about it, and Mission Control is zooming back out
// to the old desktop: the switch goes at once, so that the zoom lands on the new one. Without a click just
// before, the focus may be macOS's doing (an app quit or closed a window while Mission Control was up): it waits.
static void follow_later(uint32_t wid, bool user_call) {
    unsigned gen = ++follow_gen;
    const char *why = trigger;
    if (last_click_ago() < CALL_INPUT_S && wm_mission_control_up()) {
        follow_check_now(wid, why, user_call);
        return;
    }
    run_after(FOLLOW_DELAY_S, ^{
        if (gen == follow_gen) follow_check_now(wid, why, user_call);
    });
}

// Seconds since the last left or right mouse click.
static double last_click_ago(void) {
    double click = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateHIDSystemState, kCGEventLeftMouseDown);
    double rclick = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateHIDSystemState, kCGEventRightMouseDown);
    return rclick < click ? rclick : click;
}

// A mouse click, Return / Enter or a shortcut (Cmd-Tab, an app's own key) just before now, after the quiet period began: the user called this window.
static bool called_by_user(double now) {
    double at = now - last_click_ago(), key = hotkey_last_call_key();
    return input_is_call(at > key ? at : key, settle_start, now);
}

// A shortcut other than close, quit, minimize or hide just before now, with no click since: switch without waiting.
static bool quick_call(double now) {
    double click_at = now - last_click_ago();
    return input_is_quick_call(hotkey_last_quick_key(), click_at, now);
}

static void on_focus(bool recheck) {
    follow_gen++; // a call still waiting is stale: this event decides again
    win_t *w = wm_focused();
    if (w && !wm_here(w)) {
        // A parked window took focus (Raycast, Cmd-Tab, Dock, a notification): go to its desktop (after
        // FOLLOW_DELAY_S, see follow_later; at once after a fresh shortcut), unless macOS is still settling after a
        // desktop change or a close, or it is the one we just parked.
        double t = CFAbsoluteTimeGetCurrent();
        bool settling = t < settle_until;
        if (settling) {
            double cap = settle_start + SETTLE_MAX_S;
            settle_until = t + SETTLE_S < cap ? t + SETTLE_S : cap;
            recheck_after_settle();
        }
        // During the quiet period or at its recheck, input that calls a window up makes this a call, not macOS's doing.
        bool user_call = (settling || recheck) && called_by_user(t);
        switch (focus_act(settling, recheck, w->wid == stale, first_in(cur_ws) != NULL, user_call)) {
        case FOCUS_REFOCUS:
            log_focus(w, "refocus", cur_ws);
            focus_in_current(NULL);
            return;
        case FOCUS_STALE:
            stale = w->wid;
            last_focus[cur_ws] = 0; // nothing usable here: a later call must not be read as this desktop's window leaving
            return;
        case FOCUS_FOLLOW:
            if (quick_call(t)) follow_check_now(w->wid, trigger, user_call);
            else follow_later(w->wid, user_call);
            return;
        default:
            return;
        }
    }
    if (w) {
        stale = 0;
        if (w->ws != WS_EVERY) last_focus[cur_ws] = w->wid;
        visit(w->wid);
    }
    wm_ui_draw(w);
}

void wm_on_focus(void) { on_focus(false); }

// WindowServer may not list the windows just shown or focused as on screen yet: check again once it does.
void wm_refresh_menubar_later(void) {
    if (!menubar_ready()) return;
    run_after(FRESH_S + 0.05, ^{ wm_refresh_menubar(wm_focused()); });
}

// A `none` app with a tracked window. Keyed on the app, not the focused window: a panel such as a
// quick terminal is never tracked.
static bool every_app(pid_t pid) {
    app_t *a = wm_find_app(pid);
    if (!a || a->assign != CONFIG_EVERY) return false;
    for (int j = 0; j < nwins; j++)
        if (wins[j].pid == pid) return true;
    return false;
}

// Apps set to `none` leave with the desktop, except one with no window tracked. Called inside wm_switch, so the
// focus macOS moves to after a hide falls in the quiet period.
static void hide_every(void) {
    for (int i = 0; i < napps; i++)
        if (every_app(apps[i].pid) && apps_hide(apps[i].pid)) log_hide(apps[i].pid);
}

// t is the window focused after the change, or NULL.
static void finish_change(win_t *t, bool took) {
    wm_settle();
    wm_mark_fresh(t ? t->wid : 0, settle_start);
    // Drawn for t, not for what macOS reports: it names the window focused before for a moment yet, and the
    // ring would be missing when a switch lets the screen go. Not when t's app did not answer: asking it for
    // the ring's place would wait out another timeout, and the ring belongs to the window that kept the focus.
    if (!took) {
        wm_refresh_ui();
    } else {
        wm_ui_draw(t);
    }
    wm_refresh_menubar_later();
}

// Windows change place one app at a time: each move is a round trip to its app, and WindowServer shows it
// some tens of ms after Accessibility says it is done, so a switch showed windows leaving and popping in one
// by one. The screen is held still across the switch instead and let go when WindowServer has every moved
// window where it was sent, or HOLD_MAX_S after the moves were asked for (counted in polls, so a clock step
// cannot stretch it). Switches in quick succession share one hold. A hung app must not freeze every screen
// while its call times out: past HOLD_BUSY_S the moves go on in view. WindowServer itself lets go after about
// a second. Under Mission Control's zoom there is no hold: letting go in the middle of it tears the screen
// into stripes, and the zoom covers the moves anyway.
#define HOLD_POLL_S 0.008
#define HOLD_MAX_S 0.3
#define HOLD_BUSY_S 0.6
static unsigned hold_gen;
static bool holding;
static double held_since;
static int hold_polls; // polls left before the hold is given up

static void release_screen(void) {
    if (!holding) return;
    holding = false;
    SLSReenableUpdate(SLSMainConnectionID());
}

// The way out: a poll still queued for the hold is stale, and the screen must not stay held.
void wm_hold_drop(void) {
    hold_gen++;
    release_screen();
}

static void release_if_busy(void) {
    if (holding && CFAbsoluteTimeGetCurrent() - held_since > HOLD_BUSY_S) release_screen();
}

static void release_when_in_place(unsigned gen) {
    run_after(HOLD_POLL_S, ^{
        if (gen != hold_gen) return;
        if (wm_in_place(held_since) || --hold_polls <= 0) release_screen();
        else release_when_in_place(gen);
    });
}

void wm_switch(int ws) {
    if (ws < 1 || ws > config_workspaces() || ws == cur_ws) return;
    follow_gen++;
    win_t *f = wm_focused();
    if (f && f->ws == cur_ws) last_focus[cur_ws] = f->wid;

    int old = cur_ws;
    cur_ws = ws;
    stale = f && f->ws == old ? f->wid : 0;
    unsigned gen = ++hold_gen;
    bool zooming = wm_mission_control_up();
    if (zooming) release_screen(); // a hold left by a switch just before would be let go in the middle of the zoom
    bool began = !zooming && !holding && SLSDisableUpdate(SLSMainConnectionID()) == kCGErrorSuccess;
    if (began) {
        holding = true;
        held_since = CFAbsoluteTimeGetCurrent();
    }
    for (int i = 0; i < nwins; i++)
        if (wins[i].ws == ws) {
            release_if_busy();
            wm_show(&wins[i]);
        }
    for (int i = 0; i < nwins; i++)
        if (wins[i].ws == old) {
            release_if_busy();
            wm_hide(&wins[i]);
        }

    bool took;
    win_t *t = focus_in_current(&took);
    hide_every();
    finish_change(t, took);
    if (began) hold_polls = (int)(HOLD_MAX_S / HOLD_POLL_S);
    if (holding) {
        // Giving focus and drawing the ring above usually outlast WindowServer: look now, before the first poll,
        // which would also queue behind the focus notices this switch set off. The strip goes out first: AppKit
        // would send it only after this pass of the run loop.
        if (wm_in_place(held_since)) {
            menubar_flush();
            release_screen();
        } else {
            release_when_in_place(gen);
        }
    }
}

void wm_dismiss_every(void) {
    pid_t pid = apps_frontmost();
    if (!every_app(pid)) return;
    trigger = "hotkey";
    bool took;
    win_t *t = focus_in_current(&took);
    if (apps_hide(pid)) log_hide(pid);
    finish_change(t, took);
}

void wm_move_focused(int ws) {
    if (ws < 1 || ws > config_workspaces()) return;
    win_t *w = wm_focused();
    if (!w || w->ws == ws || w->ws == WS_EVERY) return;
    trigger = "hotkey";
    int old = w->ws;
    w->ws = ws;
    wm_log_ws(w, old, "move");
    last_focus[ws] = w->wid;
    if (ws == cur_ws) {
        // A parked window that kept focus, moved to the desktop shown now: bring it here.
        follow_gen++;
        if (w->wid == stale) stale = 0;
        wm_show(w);
        wm_focus_wid(w->wid);
        return;
    }
    wm_hide(w);
    stale = w->wid;
    focus_in_current(NULL);
    wm_settle();
    wm_refresh_ui();
}

void wm_focus_wid(uint32_t wid) {
    win_t *w = wm_find_win(wid);
    if (!w || !wm_here(w)) return;
    if (w->ws != WS_EVERY) last_focus[cur_ws] = wid;
    focus(w);
    wm_mark_fresh(wid, CFAbsoluteTimeGetCurrent());
    wm_refresh_ui();
    wm_refresh_menubar_later();
}

void wm_history_hold(void) { hold_switcher = true; }

void wm_history_release(uint32_t wid) {
    hold_switcher = false;
    if (!wid) {
        trigger = "hotkey";
        wm_on_focus(); // no focus notice follows the release, so record the window it ended on
        return;
    }
    // Asking macOS now may still name an earlier Tab's window, so record the one we focused. A late notice
    // of an earlier window stays unrecorded until wid's own notice (or the settle time) ends the hold.
    hist_visit(&hist, wid);
    hold_until = CFAbsoluteTimeGetCurrent() + SETTLE_S;
}

static bool usable(uint32_t wid) {
    win_t *w = wm_find_win(wid);
    if (!w || w->minimized) return false;
    return !(w->ws == WS_EVERY && apps_hidden(w->pid));
}

static unsigned history_gen;

void wm_history(int dir) {
    uint32_t wid = hist_step(&hist, dir, usable);
    win_t *w = wm_find_win(wid);
    if (!w) return;
    trigger = "hotkey";
    hold_until = CFAbsoluteTimeGetCurrent() + SETTLE_S;
    // If focusing wid fails, a window clicked meanwhile must still be recorded once the hold ends.
    unsigned gen = ++history_gen;
    run_after(SETTLE_S + 0.02, ^{
        if (gen == history_gen) wm_on_focus();
    });
    if (!wm_here(w)) {
        last_focus[w->ws] = wid;
        wm_switch(w->ws);
    } else {
        wm_focus_wid(wid);
    }
}
