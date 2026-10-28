#pragma once
// What to do when macOS focuses a window.
#include <stdbool.h>

typedef enum {
    FOCUS_RECORD,  // the window is on this desktop: note it
    FOCUS_FOLLOW,  // a parked window was called for: check again shortly, then switch to its desktop
    FOCUS_REFOCUS, // a parked window after a switch settled, and this desktop has windows: focus one of them
    FOCUS_STALE,   // same, but this desktop is empty: leave the parked window focused and ignore it
    FOCUS_IGNORE,  // macOS handing focus around after a switch or a close, or the window we just parked
} focus_act_t;

// user_call: the user just did something that calls a window (input_is_call). That is followed even while
// settling or at the recheck, but never the window we just parked.
focus_act_t focus_act(bool parked, bool settling, bool recheck, bool is_stale, bool cur_has_win, bool user_call);

// Whether input at `at` (CFAbsoluteTime, 0 if none) is a call for the window focused at `now`: it came after the
// quiet period began at settle_start (so not the key that switched) and at most CALL_INPUT_S before now.
#define CALL_INPUT_S 1.0
bool input_is_call(double at, double settle_start, double now);

typedef enum {
    FOLLOW_DROP,   // the call is over or handled elsewhere: do nothing
    FOLLOW_WAIT,   // a quiet period started meanwhile: the focus is dealt with when it ends
    FOLLOW_LEFT,   // macOS focused the parked window because the user's window here closed, minimized or quit
    FOLLOW_SWITCH, // the call stands: switch to the window's desktop
} follow_act_t;

// macOS focuses another window of the app about 0.7 s before it reports that the focused one was minimized,
// so a call is acted on only after a short delay, when its cause is clearer.
// still_focused: the window is still the focused one. now_here: it is on this desktop now.
// settling: a quiet period is active. cur_left: the window last used on this desktop is minimized, closed or its app quit.
// user_call: the call came from user input (see focus_act): it stands even while settling.
follow_act_t follow_check(bool still_focused, bool now_here, bool settling, bool cur_left, bool user_call);

typedef enum {
    READ_OK,      // the window answered
    READ_INVALID, // its element is invalid: it was closed
    READ_BUSY,    // its app did not answer in time: busy, hung or quitting
    READ_OTHER,   // any other error
} ax_read_t;

// Whether the window last used on this desktop was just minimized, closed or its app quit, before its
// notice arrived. Once the notice has been handled it is no longer in flight: a minimized window still tracked,
// or one already removed, left long ago.
// tracked_here: the window is still tracked and on this desktop. known_minimized: its minimized flag is set.
// alive: its app is running. read, ax_minimized: the outcome of reading AXMinimized from it.
bool cur_left(bool tracked_here, bool known_minimized, bool alive, ax_read_t read, bool ax_minimized);
