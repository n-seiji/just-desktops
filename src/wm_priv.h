#pragma once
// What wm.c, wm_focus.c, wm_park.c and wm_list.c share. Other modules use wm.h.
#include "common.h"
#include <dispatch/dispatch.h>

typedef struct {
    AXUIElementRef el;
    pid_t pid;
    uint32_t wid;
    int ws;
    CGPoint saved;
    bool has_saved; // saved holds a place to return to
    bool hidden;
    double shown_at; // when show last unparked it
    double parked_at; // when wm_hide last moved it to the corner
    bool was_listed; // WindowServer listed it the last time we looked
    bool minimized;  // kept from the (de)miniaturize notifications: asking a hung app costs a timeout per window
} win_t;

typedef struct {
    pid_t pid;
    AXUIElementRef el;
    AXObserverRef obs;
    int assign;
    bool nopreview;
} app_t;

extern win_t *wins; // in tracking order: cycling relies on it
extern int nwins;
extern app_t *apps;
extern int napps;
extern int cur_ws;  // the current desktop

static inline bool wm_here(const win_t *w) { return w->ws == cur_ws || w->ws == WS_EVERY; }

static inline void run_after(double seconds, dispatch_block_t block) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, seconds * NSEC_PER_SEC), dispatch_get_main_queue(), block);
}

// wm.c: tracked apps and windows
app_t *wm_find_app(pid_t pid);
win_t *wm_find_win(uint32_t wid);
win_t *wm_focused(void); // the window macOS reports as focused, tracked now if it was not

// wm_park.c: moving windows to the corner and back
void wm_hide(win_t *w);
AXError wm_show(win_t *w); // moves it back to its place; the error of the move
// The display windows are parked at the corner of (see shown_park_display); the main display if none is listed.
CGRect wm_park_display(void);

// Whether WindowServer has every window moved at or after `since` where it was sent: the unparked ones out
// of the corner, the parked ones in it. Accessibility reports a move as done at once; the window itself
// follows some tens of ms later. True when WindowServer cannot be asked.
bool wm_in_place(double since);

// wm_focus.c: switching, focus, the hold, the border and the ws log
void wm_hold_drop(void); // the way out: let the screen go and cancel the polls waiting on the hold
void wm_log_ws(const win_t *w, int old, const char *reason); // os_log a desktop change (old -1: first tracked)
void wm_log_recover(const win_t *w); // os_log a window found parked by a previous run
void wm_log_rescan(int n); // os_log the look after an unlock, with the number of windows it added
void wm_log_repark(int n); // os_log n parked windows sent to the new corner
void wm_log_reshow(const win_t *w, CGPoint want, CGPoint got); // os_log an unparked window that was not where it was sent

// wm_list.c: window lists and the menu bar
bool wm_mission_control_up(void);

void wm_refresh_menubar(win_t *f);
// The switchable windows of this desktop for the menu bar: pids and wids hold JD_MENUBAR_WINS. The first
// *shown_n get a slot, the next *over are behind the "+k" one, and *hash tells that set apart (see order.h).
void wm_menubar_rows(pid_t *pids, uint32_t *wids, int *shown_n, int *over, uint32_t *hash);
void wm_ui_draw(win_t *w); // the ring and the menu bar for w, the focused window looked up once by the caller
void wm_ui_menubar_now(void); // the menu bar for the focused window, if it is up
void wm_refresh_menubar_later(void); // again once WindowServer lists the windows just shown or focused
void wm_mark_fresh(uint32_t wid, double now); // the menu bar prefers wid to macOS's lagging focus for a moment
