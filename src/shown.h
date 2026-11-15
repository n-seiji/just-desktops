#pragma once
// Which windows count as shown right now, and which one the menu bar calls focused.
#include <ApplicationServices/ApplicationServices.h>
#include <stdbool.h>
#include <stdint.h>

#define FRESH_S 0.3 // WindowServer takes about this long to list windows we just unparked

// WindowServer's view of a window: not transparent, with at least 32pt (or all of it, when smaller)
// inside one of the displays. A window parked at the corner of the park display (below) fails this.
bool shown_visible(double alpha, CGRect window, const CGRect *displays, uint32_t ndisplays);

// A window unparked less than FRESH_S ago may not be listed as on screen yet: it counts as listed
// when it was before. listed is what WindowServer says now; *was_listed is what it said the last
// time the window was past that period, and is updated then.
bool shown_grace(bool listed, bool *was_listed, double now, double shown_at);

// The display whose bottom-right corner windows are parked at: the one reaching furthest right, and of
// those the one reaching furthest down. Nothing lies to the right of or below that corner, so a window
// with its origin there is off every display. With n 0, displays is not read: returns CGRectZero.
CGRect shown_park_display(const CGRect *displays, uint32_t n);

// Windows of inactive desktops are parked at the bottom-right corner of the park display. This is
// the point a window's origin is moved to, and whether an origin is at it. macOS and apps keep some of
// the window in reach, so a parked origin is seen up to 41pt above the point and 17pt left of it. An
// origin past the corner is not parked: with a second display to the lower right it is in plain view.
CGPoint shown_park_point(CGRect display);
bool shown_is_parked(CGPoint origin, CGRect display);

// Where a window found in the corner with no known place to return to is put: near the top-left of the display
// (the main display, not the park display).
CGPoint shown_safe_point(CGRect display);

// The point to remember as a window's place when it is about to be parked; read is its position now. A
// window in the corner, or one unparked at most FRESH_S ago (its app may not have applied the move yet, and
// nobody places a window that fast), keeps the point it has; with none, a window in the corner gets the safe
// point (of main_display). Otherwise it is where the window is now. park is the park display.
CGPoint shown_return_point(CGPoint read, CGRect park, CGRect main_display, bool has_saved, CGPoint saved, double since_shown);

// Whether a window asked to go to want ended up elsewhere: more than 1pt off on either axis.
bool shown_missed(CGPoint want, CGPoint got);

// The window the menu bar marks: the one just focused by us until FRESH_S has passed, because the
// focus macOS reports lags behind, else the reported one. 0 for none.
uint32_t shown_front(double now, double fresh_until, uint32_t fresh_wid, uint32_t focused_wid);
