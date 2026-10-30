#pragma once
#include "common.h"
#include <CoreText/CoreText.h>
#include <dispatch/dispatch.h>

// Drawing and window helpers shared by the list (switcher.c) and card (switcher_card.c) switchers.

// The panel's colours; the dark one is chosen unless the system is in Light mode.
typedef struct {
    uint32_t tint;
    CGFloat tint_a;
    CGFloat edge_gray, edge_top, edge_bottom; // the border's colour and its alpha at the top and the flat rim
    uint32_t header, title, highlight;
    CGFloat highlight_a;
} palette_t;

const palette_t *sw_palette(void); // by the system appearance right now

CTFontRef sw_font(CGFloat size);
void sw_color(CGContextRef c, uint32_t rgb, CGFloat a, bool stroke);
// One line of text in font, cut with an ellipsis to maxw (at the end, or at the start for paths).
CTLineRef sw_fit(CTFontRef font, CFStringRef s, uint32_t rgb, CGFloat maxw, CTLineTruncationType how);
// Draws l with its x-height-ish middle on cy, then releases it. The context is y-down.
void sw_put(CGContextRef c, CTLineRef l, CGFloat x, CGFloat cy);
// Clears c (of size sz) and makes it y-down; sw_finish undoes that and flushes it.
void sw_begin(CGContextRef c, CGSize sz);
void sw_finish(CGContextRef c);
void sw_fill_rrect(CGContextRef c, CGRect r, CGFloat radius, uint32_t rgb, CGFloat a);
// A 1pt border drawn inside r, lit along the top and fading to a flat rim.
void sw_edge_rrect(CGContextRef c, CGRect r, CGFloat radius, const palette_t *pal);
// A translucent, mouse-transparent window at at of size sz, drawn at res (2 is sharp on Retina, 1 costs a quarter of the memory). With radius > 0 the background
// is blurred and the window takes the rounded shape.
uint32_t sw_window(int cid, CGPoint at, CGSize sz, CGFloat radius, double res, CGContextRef *out);
// Orders out and releases a window from sw_window with its context; a no-op when *wid is 0.
void sw_close_window(int cid, uint32_t *wid, CGContextRef *ctx);
// A 60 Hz main-queue timer calling fn; start is a no-op while *t runs, stop a no-op when it doesn't.
void sw_timer_start(dispatch_source_t *t, void (*fn)(void));
void sw_timer_stop(dispatch_source_t *t);

// Frees the entries wm_windows or wm_overflow returned.
void sw_free_entries(sw_entry_t *ents, int n);
