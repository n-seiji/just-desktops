#pragma once
// What wm.c, wm_focus.c and wm_list.c share. Other modules use wm.h.
#include "common.h"

typedef struct {
    AXUIElementRef el;
    pid_t pid;
    uint32_t wid;
    int ws;
    CGPoint saved;
    bool hidden;
    double shown_at; // when show last unparked it
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

// wm.c: tracked apps and windows
app_t *wm_find_app(pid_t pid);
win_t *wm_find_win(uint32_t wid);
win_t *wm_focused(void); // the window macOS reports as focused, tracked now if it was not

// wm_focus.c: parking, focus and the border
void wm_hide(win_t *w);
void wm_log_ws(const win_t *w, int old, const char *reason); // os_log a desktop change (old -1: first tracked)
void wm_log_recover(const win_t *w); // os_log a window found parked by a previous run

// wm_list.c: window lists and the menu bar
void wm_refresh_menubar(win_t *f);
void wm_mark_fresh(uint32_t wid, double now); // the menu bar prefers wid to macOS's lagging focus for a moment
