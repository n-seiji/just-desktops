#pragma once
// Geometry of the menu bar strip: one square slot per glyph, a "+k" slot after them when windows
// do not fit, and the desktop's key box last. The item reserves room for
// STRIP_MIN_SLOTS slots (more while it was needed lately) and the strip sits at its right end.
#include <stdbool.h>

enum { STRIP_SLOT = 20, STRIP_GAP = 4 }; // points
enum { STRIP_MIN_SLOTS = 5 };            // slots the item always has room for
enum { STRIP_SHRINK_S = 30 };            // seconds the extra width stays after it was last needed

// Slots that hold a window glyph or the "+k" count, without the key box.
int strip_slots(int n, int over);
// Width in points of a strip with that many slots before the key box. The item's image is this wide
// for the reserved slots.
int strip_width(int slots);
// The reserved slots after a change: at least STRIP_MIN_SLOTS, grows to need at once, falls from
// reserve to need only when expired (the shrink timer fired).
int strip_reserve(int need, int reserve, bool expired);

typedef struct {
    int slot;      // the glyph under the pointer, or -1
    bool overflow; // the "+k" slot is under the pointer
} strip_hit_t;
// What is at x (points from the left edge of the button, whose width is button_w) in a strip of n
// glyphs and, when over > 0, a "+k" slot, in an image of reserve slots centred in the button. Gaps, the key box and everything outside hit nothing.
strip_hit_t strip_hit(double x, double button_w, int reserve, int n, int over);
// Left edge of the "+k" slot in button coordinates.
double strip_overflow_left(double button_w, int reserve, int n, int over);
