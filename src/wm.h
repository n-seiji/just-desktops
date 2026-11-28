#pragma once
// Desktops emulated by parking the windows of inactive desktops at the bottom-right corner of the display furthest right.
#include "common.h"

void wm_init(void);
void wm_add_app(pid_t pid, const char *bundle_id);
void wm_remove_app(pid_t pid);
void wm_rescan(void); // track the windows of every registered app that are not tracked yet: the screen unlocked
void wm_on_focus(void); // the focused window or app may have changed
void wm_refresh_ui(void); // redraw the border and menu bar for the focused window
void wm_set_trigger(const char *what); // what led to the next wm_on_focus, a literal; only for the log
void wm_settle(void); // macOS is about to move focus on its own: parked windows it focuses for a while are not followed
void wm_settle_pid(pid_t pid); // pid quit: if it had a window here, the focus macOS moves next is not followed
void wm_switch(int ws);
void wm_displays_changed(void); // displays were added, removed or moved: parked windows go to the new corner
void wm_dismiss_every(void); // the frontmost `none` app is hidden alone and focus returns to the current desktop
void wm_move_focused(int ws);
int wm_current(void);
void wm_focus_wid(uint32_t wid);
void wm_history(int dir); // -1 back, +1 forward through the focus history, across desktops
void wm_history_hold(void); // windows focused by ⌥Tab are not recorded until wm_history_release
void wm_history_release(uint32_t wid); // records wid, the window ⌥Tab ended on (0: whatever macOS reports focused)
void wm_restore_all(void); // every parked window back on screen, in 2.5 s at most: for the way out
char *wm_membership(void); // the desktops as text for the next run; malloc'd, NULL on failure
void wm_restore_membership(const char *text); // apply that text; before apps_init

// The switchable windows of this desktop for the switchers, in menu bar order. *out is malloc'd and
// each entry has +1 icon, app and title (the title is the row text); the caller releases them with
// sw_free_entries (common.h). *sel is the window after the focused one (before it for dir < 0).
int wm_windows(int dir, sw_entry_t **out, int *sel);
int wm_window_ids(int dir, uint32_t **out, int *sel); // wm_windows without icons and text; *out is NULL when 0, else the caller frees it
// The windows behind the menu bar's "+k" slot, in order, as wm_windows has them.
int wm_overflow(sw_entry_t **out);
