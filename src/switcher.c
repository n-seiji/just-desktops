#include "switcher.h"
#include "cardmath.h"
#include "config.h"
#include "select.h"
#include "skylight.h"
#include "switcher_card.h"
#include "swkit.h"
#include "wm.h"
#include <dispatch/dispatch.h>
#include <malloc/malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Window switcher panel (hold ⌥, press Tab; with `switcher = off` no panel, see instant_step). The windows, their contexts, the fonts,
// the icons and the entries exist only while the panel is open; the animation
// timer runs only while the highlight is moving.
//
// The highlight is its own small window that slides over the panel. Flushing the
// panel's context again makes SkyLight keep up to two more full-size backing
// surfaces (~4.6MB each) for about a second, so the panel is flushed when it opens and
// again only when the list scrolls (top changes); the animation only moves the highlight window.

#define MIN_W 420.0
#define MAX_W 750.0
#define RADIUS 26.0
#define HEADER_H 34.0
#define ROW_H 46.0
#define ROW_INSET 8.0
#define ROW_PAD 8.0
#define ROW_RADIUS 12.0
#define ICON 28.0
#define ICON_GAP 14.0
#define MAX_ROWS 9
#define SLIDE_MS 120.0
#define SHOW_DELAY_MS 150 // the panel appears only after ⌥ has been held this long since the first Tab

enum { F_HEADER, F_ROW, NFONTS };
static const CGFloat font_sizes[NFONTS] = {13, 16};

static int cid;
static uint32_t wid, hwid;
static CGContextRef ctx, hctx;
static CTFontRef fonts[NFONTS];
static dispatch_source_t timer;
static sw_entry_t *ents;
static int n, sel, top, rows;
static bool hl_last;
static const palette_t *pal;
static CGSize size;
static CGPoint origin;
static double pos, from_pos, t0;

// The row text of an entry: its cleaned title, or the app name when there is none.
static CFStringRef row_text(const sw_entry_t *e) {
    return e->title ? e->title : e->app;
}

static bool is_path(CFStringRef s) {
    return CFStringHasPrefix(s, CFSTR("~/")) || CFStringHasPrefix(s, CFSTR("/"));
}

// The panel's width: the widest title plus the row's padding, within MIN_W..MAX_W.
static CGFloat fit_width(void) {
    CGFloat w = 0;
    for (int i = 0; i < n; i++) {
        CFStringRef s = row_text(&ents[i]);
        if (!s) continue;
        CTLineRef l = sw_fit(fonts[F_ROW], s, 0, 1e9, kCTLineTruncationEnd);
        w = fmax(w, CTLineGetTypographicBounds(l, NULL, NULL, NULL));
        CFRelease(l);
    }
    w = ceil(2 * (ROW_INSET + ROW_PAD) + ICON + ICON_GAP + w);
    return fmin(fmax(w, MIN_W), MAX_W);
}

static void draw_header(CGContextRef c) {
    char buf[32];
    snprintf(buf, sizeof buf, "Desktop %d", wm_current());
    CFStringRef s = CFStringCreateWithCString(NULL, buf, kCFStringEncodingUTF8);
    sw_put(c, sw_fit(fonts[F_HEADER], s, pal->header, size.width - 2 * (ROW_INSET + ROW_PAD), kCTLineTruncationEnd), ROW_INSET + ROW_PAD, 19);
    CFRelease(s);
}

static void draw_row(CGContextRef c, sw_entry_t *e, CGFloat y) {
    CGFloat cy = y + ROW_H / 2, left = ROW_INSET + ROW_PAD + ICON + ICON_GAP, right = size.width - ROW_INSET - ROW_PAD;
    if (e->icon) {
        CGContextSaveGState(c);
        CGContextTranslateCTM(c, ROW_INSET + ROW_PAD, cy + ICON / 2);
        CGContextScaleCTM(c, 1, -1);
        CGContextDrawImage(c, (CGRect){{0, 0}, {ICON, ICON}}, e->icon);
        CGContextRestoreGState(c);
    }
    CFStringRef main = row_text(e);
    if (!main) return;
    sw_put(c, sw_fit(fonts[F_ROW], main, pal->title, right - left, is_path(main) ? kCTLineTruncationStart : kCTLineTruncationEnd), left, cy);
}

// All of the panel's looks live here. Between sw_begin and sw_finish the context is y-down.
static void draw(CGContextRef c) {
    CGRect all = {{0, 0}, size};
    sw_begin(c, size);

    sw_fill_rrect(c, all, RADIUS, pal->tint, pal->tint_a);
    sw_edge_rrect(c, all, RADIUS, pal);

    draw_header(c);
    for (int r = 0; r < rows; r++) draw_row(c, &ents[top + r], HEADER_H + r * ROW_H);
    sw_finish(c);
}

static CGSize hl_size(void) {
    return CGSizeMake(size.width - 2 * ROW_INSET, ROW_H);
}

// The bottom row's highlight sits concentric with the panel's bottom corners.
static void draw_highlight(CGContextRef c) {
    CGSize sz = hl_size();
    CGFloat br = hl_last ? RADIUS - ROW_INSET : ROW_RADIUS;
    sw_begin(c, sz);
    CGContextMoveToPoint(c, ROW_RADIUS, 0);
    CGContextAddArcToPoint(c, sz.width, 0, sz.width, br, ROW_RADIUS);
    CGContextAddArcToPoint(c, sz.width, sz.height, sz.width - br, sz.height, br);
    CGContextAddArcToPoint(c, 0, sz.height, 0, sz.height - br, br);
    CGContextAddArcToPoint(c, 0, 0, ROW_RADIUS, 0, ROW_RADIUS);
    CGContextClosePath(c);
    sw_color(c, pal->highlight, pal->highlight_a, false);
    CGContextFillPath(c);
    sw_finish(c);
}

static CGPoint hl_origin(void) {
    double hy = fmin(fmax(pos - top, 0), rows - 1);
    return CGPointMake(origin.x + ROW_INSET, origin.y + HEADER_H + hy * ROW_H);
}

static void close_panel(void) {
    sw_timer_stop(&timer);
    sw_close_window(cid, &wid, &ctx);
    sw_close_window(cid, &hwid, &hctx);
    for (int i = 0; i < NFONTS; i++) {
        if (fonts[i]) CFRelease(fonts[i]);
        fonts[i] = NULL;
    }
    sw_free_entries(ents, n);
    ents = NULL;
    n = 0;
    malloc_zone_pressure_relief(NULL, 0);
}

static void tick(void) {
    double k = (CFAbsoluteTimeGetCurrent() - t0) * 1000 / SLIDE_MS;
    pos = cardmath_slide(from_pos, sel, k);
    if (k >= 1) sw_timer_stop(&timer);
    CGPoint o = hl_origin();
    SLSMoveWindow(cid, hwid, &o);
}

static void open_panel(int dir) {
    n = wm_windows(dir, &ents, &sel);
    if (!n) {
        close_panel();
        return;
    }
    cid = SLSMainConnectionID();
    pal = sw_palette();
    bool ok = true;
    for (int i = 0; i < NFONTS; i++) ok = (fonts[i] = sw_font(font_sizes[i])) && ok;
    if (!ok) {
        close_panel();
        return;
    }
    rows = n < MAX_ROWS ? n : MAX_ROWS;
    top = sel_top(0, sel, rows);
    pos = from_pos = sel;
    size = CGSizeMake(fit_width(), HEADER_H + rows * ROW_H + ROW_INSET);
    CGRect d = CGDisplayBounds(CGMainDisplayID());
    origin = CGPointMake(d.origin.x + (d.size.width - size.width) / 2, d.origin.y + (d.size.height - size.height) / 2);
    hl_last = sel - top == rows - 1;
    wid = sw_window(cid, origin, size, RADIUS, 2, &ctx);
    CGPoint o = hl_origin();
    hwid = wid ? sw_window(cid, o, hl_size(), 0, 2, &hctx) : 0;
    if (!hwid || !ctx || !hctx) {
        close_panel();
        return;
    }
    draw(ctx);
    draw_highlight(hctx);
    SLSOrderWindow(cid, wid, 1, 0);
    SLSOrderWindow(cid, hwid, 1, wid);
}

static void panel_step(int dir) {
    if (config_switcher_style() == SW_SQUARE) {
        card_step(dir);
        return;
    }
    if (!wid) {
        open_panel(dir);
        return;
    }
    from_pos = pos;
    sel = sel_wrap(sel + dir, n);
    int old = top;
    top = sel_top(top, sel, rows);
    if (top != old) draw(ctx);
    bool last = sel - top == rows - 1;
    if (last != hl_last) {
        hl_last = last;
        draw_highlight(hctx);
    }
    t0 = CFAbsoluteTimeGetCurrent();
    sw_timer_start(&timer, tick);
}

static void panel_commit(void) {
    if (config_switcher_style() == SW_SQUARE) {
        card_commit();
        return;
    }
    if (!wid) return;
    uint32_t target = ents[sel].wid;
    close_panel();
    wm_focus_wid(target);
}

static void panel_cancel(void) {
    if (config_switcher_style() == SW_SQUARE) {
        card_cancel();
        return;
    }
    if (wid) close_panel();
}

// A tap of ⌥Tab must not flash the panel. From the first Tab until the delay is over only the
// selection is tracked (the first direction and the net of the later Tabs); no window, font or
// icon exists. The timer opens the panel and replays the extra steps.
static dispatch_source_t delay;
static bool waiting, visible;
static int first_dir, extra;

static void stop_delay(void) {
    if (!delay) return;
    dispatch_source_cancel(delay);
    dispatch_release(delay);
    delay = NULL;
}

static void show(void) {
    stop_delay();
    waiting = false;
    visible = true;
    panel_step(first_dir);
    for (int i = 0; i < abs(extra); i++) panel_step(extra > 0 ? 1 : -1);
}

// switcher = off: no panel at all. The first Tab snapshots the window ids (same list and order as the
// panels) and every Tab focuses the next one at once; the snapshot only lives until ⌥ is let go.
static uint32_t *snap;
static int snap_n, snap_at;

static void instant_end(void) {
    uint32_t end = snap ? snap[snap_at] : 0;
    free(snap);
    snap = NULL;
    snap_n = 0;
    wm_history_release(end);
}

static void instant_step(int dir) {
    wm_history_hold();
    if (!snap) {
        int start;
        snap_n = wm_window_ids(dir, &snap, &start);
        snap_at = start;
        if (!snap_n) instant_end();
    } else {
        snap_at = sel_wrap(snap_at + dir, snap_n);
    }
    if (snap) wm_focus_wid(snap[snap_at]);
}

void switcher_step(int dir) {
    if (config_switcher_style() == SW_OFF) {
        instant_step(dir);
        return;
    }
    if (visible) {
        panel_step(dir);
        return;
    }
    if (waiting) {
        extra += dir;
        return;
    }
    waiting = true;
    first_dir = dir;
    extra = 0;
    delay = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_timer(delay, dispatch_time(DISPATCH_TIME_NOW, SHOW_DELAY_MS * NSEC_PER_MSEC), DISPATCH_TIME_FOREVER, 5 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(delay, ^{ show(); });
    dispatch_resume(delay);
}

void switcher_commit(void) {
    if (config_switcher_style() == SW_OFF) {
        instant_end();
        return;
    }
    if (!waiting) {
        visible = false;
        panel_commit();
        return;
    }
    stop_delay();
    waiting = false;
    uint32_t *ids = NULL;
    int start, count = wm_window_ids(first_dir, &ids, &start);
    if (count) wm_focus_wid(ids[sel_wrap(start + extra, count)]);
    free(ids);
}

void switcher_cancel(void) {
    if (config_switcher_style() == SW_OFF) {
        instant_end();
        return;
    }
    stop_delay();
    waiting = visible = false;
    panel_cancel();
}
