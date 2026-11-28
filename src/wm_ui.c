#include "wm.h"
#include "wm_priv.h"
#include "apps.h"
#include "ax.h"
#include "border.h"
#include "config.h"
#include "menubar.h"
#include "shown.h"

// Redrawing the focus ring and the menu bar. Both are drawn for a window the caller looked up once (each
// lookup is a round trip to its app), and wm_focus.c decides when.

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

// WindowServer may not list the windows just shown or focused as on screen yet: check again once it does.
void wm_refresh_menubar_later(void) {
    if (!menubar_ready()) return;
    run_after(FRESH_S + 0.05, ^{ wm_refresh_menubar(wm_focused()); });
}

static double fresh_until; // until then, the menu bar prefers the window we just focused...
static uint32_t fresh_wid; // ...to the one macOS reports as focused, which lags behind

void wm_mark_fresh(uint32_t wid, double now) {
    fresh_until = now + FRESH_S;
    fresh_wid = wid;
}

void wm_refresh_menubar(win_t *f) {
    if (!menubar_ready()) return; // apps_init runs before menubar_init: the first draw is wm_on_focus in main
    double now = CFAbsoluteTimeGetCurrent();
    uint32_t front = shown_front(now, fresh_until, fresh_wid, f ? f->wid : 0);
    pid_t pids[JD_MENUBAR_WINS];
    uint32_t wids[JD_MENUBAR_WINS];
    int shown_n, over;
    uint32_t hash;
    wm_menubar_rows(pids, wids, &shown_n, &over, &hash);
    menubar_update(cur_ws, pids, wids, shown_n, over, hash, front);
}
