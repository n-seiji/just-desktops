#include "wm.h"
#include "wm_priv.h"
#include "apps.h"
#include "ax.h"
#include "menubar.h"
#include "order.h"
#include "select.h"
#include "shown.h"
#include "title.h"
#include <stdlib.h>
#include <string.h>

// The windows of the current desktop as the switchers and the menu bar list them.

static double fresh_until; // until then, the menu bar prefers the window we just focused...
static uint32_t fresh_wid; // ...to the one macOS reports as focused, which lags behind

void wm_mark_fresh(uint32_t wid, double now) {
    fresh_until = now + FRESH_S;
    fresh_wid = wid;
}

// Replaces each entry's title with its row text: see title_labels.
static void label_entries(sw_entry_t *list, int n) {
    int *at = malloc(sizeof *at * n);
    CFStringRef *titles = malloc(sizeof *titles * n * 2), *labels = titles + n;
    if (at && titles) {
        for (int i = 0; i < n; i++) {
            bool first = list[i].app != NULL;
            for (int j = 0; j < i && first; j++) first = !list[j].app || !CFEqual(list[j].app, list[i].app);
            if (!first) continue;
            int k = 0;
            for (int j = i; j < n; j++) {
                if (!list[j].app || !CFEqual(list[j].app, list[i].app)) continue;
                at[k] = j;
                titles[k++] = list[j].title;
            }
            title_labels(list[i].app, titles, k, labels);
            for (int m = 0; m < k; m++) {
                if (list[at[m]].title) CFRelease(list[at[m]].title);
                list[at[m]].title = labels[m];
            }
        }
    }
    free(at);
    free(titles);
}

// WindowServer's list of the windows on screen right now. NULL when it is unavailable.
static CFArrayRef shown_windows(void) {
    return CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                      kCGNullWindowID);
}

// Whether wid is really shown: in that list, not transparent, and with a visible part on a display (so not
// a hidden app's window, one on another Space, alpha 0 or off screen). True when the list is unavailable.
static bool is_shown(CFArrayRef shown, uint32_t wid) {
    if (!shown) return true;
    CGDirectDisplayID ds[8];
    uint32_t nd = 0;
    CGGetActiveDisplayList(8, ds, &nd);
    CGRect bounds[8];
    for (CFIndex i = 0; i < CFArrayGetCount(shown); i++) {
        CFDictionaryRef d = CFArrayGetValueAtIndex(shown, i);
        int num = 0;
        CFNumberRef n = CFDictionaryGetValue(d, kCGWindowNumber);
        if (!n || !CFNumberGetValue(n, kCFNumberIntType, &num) || (uint32_t)num != wid) continue;
        double alpha = 1;
        n = CFDictionaryGetValue(d, kCGWindowAlpha);
        if (n) CFNumberGetValue(n, kCFNumberDoubleType, &alpha);
        CGRect r;
        CFDictionaryRef b = CFDictionaryGetValue(d, kCGWindowBounds);
        if (alpha <= 0 || !b || !CGRectMakeWithDictionaryRepresentation(b, &r)) return false;
        for (uint32_t k = 0; k < nd; k++) bounds[k] = CGDisplayBounds(ds[k]);
        return shown_visible(alpha, r, bounds, nd);
    }
    return false;
}

// On this desktop, not minimized or parked.
static bool on_desk(const win_t *w) {
    return wm_here(w) && !w->hidden && !w->minimized;
}

// The switchable windows in menu bar order: see order_group. pids and wids need room for nwins
// (at least 1); *at is f's index in them, or -1. Returns the count, or -1 when out of memory.
static int switch_order(const win_t *f, pid_t *pids, uint32_t *wids, int *at) {
    int cap = nwins ? nwins : 1;
    pid_t *tpid = malloc(sizeof *tpid * cap);
    uint32_t *twid = malloc(sizeof *twid * cap);
    int n = -1;
    if (tpid && twid) {
        n = 0;
        CFArrayRef shown = shown_windows();
        for (int i = 0; i < nwins; i++) {
            if (!on_desk(&wins[i]) || !is_shown(shown, wins[i].wid)) continue;
            tpid[n] = wins[i].pid;
            twid[n++] = wins[i].wid;
        }
        if (shown) CFRelease(shown);
        order_group(tpid, twid, n, pids, wids);
        *at = -1;
        for (int i = 0; i < n && f; i++)
            if (wids[i] == f->wid) *at = i;
    }
    free(tpid);
    free(twid);
    return n;
}

// A row for a window: app name, cleaned title (labelled later by label_entries), icon_px icon
// (0 for none), nopreview.
static sw_entry_t entry(uint32_t wid, pid_t pid, int icon_px) {
    win_t *w = wm_find_win(wid);
    app_t *own = wm_find_app(pid);
    CFStringRef app = apps_name(pid), raw = w ? ax_title(w->el) : NULL;
    sw_entry_t e = {wid, pid, icon_px ? apps_icon(pid, icon_px) : NULL, app,
                    raw ? title_clean(raw, app) : NULL, own && own->nopreview};
    if (raw) CFRelease(raw);
    return e;
}

// Every switchable window, in the menu bar's order (grouped by app), so Tab walks the strip left
// to right.
int wm_windows(int dir, sw_entry_t **out, int *sel) {
    win_t *f = wm_focused(); // first: it may track a new window, and nwins sizes the arrays
    int cap = nwins ? nwins : 1, at = -1;
    sw_entry_t *list = malloc(sizeof *list * cap);
    pid_t *pids = malloc(sizeof *pids * cap);
    uint32_t *wids = malloc(sizeof *wids * cap);
    int n = list && pids && wids ? switch_order(f, pids, wids, &at) : -1;
    if (n < 0) {
        free(list);
        free(pids);
        free(wids);
        return 0;
    }
    for (int i = 0; i < n; i++) list[i] = entry(wids[i], pids[i], 56);
    free(pids);
    free(wids);
    label_entries(list, n);
    *sel = sel_start(at, dir, n);
    *out = list;
    return n;
}

// The same windows in the same order as wm_windows, as bare ids: no icons, names or titles.
// *out is malloc'd only when the count is > 0, else NULL; *sel as in wm_windows.
int wm_window_ids(int dir, uint32_t **out, int *sel) {
    *out = NULL;
    win_t *f = wm_focused();
    int cap = nwins ? nwins : 1, at = -1;
    pid_t *pids = malloc(sizeof *pids * cap);
    uint32_t *ids = malloc(sizeof *ids * cap);
    int n = pids && ids ? switch_order(f, pids, ids, &at) : -1;
    free(pids);
    if (n <= 0) {
        free(ids);
        *sel = 0;
        return 0;
    }
    *sel = sel_start(at, dir, n);
    *out = ids;
    return n;
}

// The switchable windows of this desktop for the menu bar, grouped by app in order of each
// app's first window, an app's windows in tracking order, so focus changes never reorder.
// Returns the count; pids and wids hold up to JD_MENUBAR_WINS.
static int menubar_order(pid_t *pids, uint32_t *wids) {
    double now = CFAbsoluteTimeGetCurrent();
    pid_t vpid[JD_MENUBAR_WINS];
    uint32_t vwid[JD_MENUBAR_WINS];
    int nv = 0;
    CFArrayRef shown = shown_windows();
    for (int i = 0; i < nwins && nv < JD_MENUBAR_WINS; i++) {
        win_t *w = &wins[i];
        if (!on_desk(w)) continue;
        if (!shown_grace(is_shown(shown, w->wid), &w->was_listed, now, w->shown_at)) continue;
        vpid[nv] = w->pid;
        vwid[nv++] = w->wid;
    }
    if (shown) CFRelease(shown);
    order_group(vpid, vwid, nv, pids, wids);
    return nv;
}

void wm_refresh_menubar(win_t *f) {
    if (!menubar_ready()) return; // apps_init runs before menubar_init: the first draw is wm_on_focus in main
    double now = CFAbsoluteTimeGetCurrent();
    uint32_t front = shown_front(now, fresh_until, fresh_wid, f ? f->wid : 0);
    pid_t pids[JD_MENUBAR_WINS];
    uint32_t wids[JD_MENUBAR_WINS];
    int n = menubar_order(pids, wids), shown_n, over;
    order_split(n, JD_MENUBAR_SLOTS, &shown_n, &over);
    menubar_update(cur_ws, pids, wids, shown_n, over, order_hash(wids + shown_n, over), front);
}

// The windows behind the menu bar's "+k" slot, in menu bar order, with +1 icon/app/title
// as wm_windows has. *out is malloc'd; the caller releases and frees. Titles are labelled
// across all of the desktop's windows, so an app with one hidden window and others shown
// still gets its title.
int wm_overflow(sw_entry_t **out) {
    pid_t pids[JD_MENUBAR_WINS];
    uint32_t wids[JD_MENUBAR_WINS];
    int n = menubar_order(pids, wids), skip, over;
    order_split(n, JD_MENUBAR_SLOTS, &skip, &over);
    *out = NULL;
    if (!over) return 0;
    sw_entry_t *list = calloc(n, sizeof *list);
    if (!list) return 0;
    for (int i = 0; i < n; i++) list[i] = entry(wids[i], pids[i], 0);
    label_entries(list, n);
    for (int i = 0; i < skip; i++) {
        if (list[i].app) CFRelease(list[i].app);
        if (list[i].title) CFRelease(list[i].title);
    }
    memmove(list, list + skip, sizeof *list * (n - skip));
    for (int i = 0; i < n - skip; i++) list[i].icon = apps_icon(list[i].pid, 36);
    *out = list;
    return over;
}
