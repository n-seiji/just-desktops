#include "switcher_card.h"
#include "appearance.h"
#include "apps.h"
#include "cardmath.h"
#include "select.h"
#include "skylight.h"
#include "swkit.h"
#include "thumb.h"
#include "wm.h"
#include <dispatch/dispatch.h>
#include <malloc/malloc.h>
#include <math.h>
#include <stdio.h>

// Square switcher. One glass square on the main display with one window shown large (its
// screenshot, or its app icon until there is one), a counter and a row of dots. Like the list panel,
// every large surface is drawn once per open: the stage is flushed once, each card is its own window
// that is moved while it fades, and the counter and the dots are tiny windows redrawn per step.
// Everything exists only while ⌥ is held after a Tab; the animation timer runs only while cards move.
//
// The windows of the entries form a strip with unwrapped positions v (the entry is v mod n).
// A card holds one position and sits at p = v - sel_v slots from the centre. A card slides a short
// way while it fades, so it never hangs far outside the stage: SkyLight does not clip a window's
// drawn content to SLSSetWindowShape, and a shape change blanks the window until it is flushed again.

#define STAGE_W 640.0
#define STAGE_H 420.0
#define STAGE_R 40.0
#define THUMB_W 576.0
#define THUMB_H 330.0
#define THUMB_R 12.0
#define THUMB_X 32.0 // the thumbnail box in the stage
#define THUMB_Y 56.0
#define CARD_PADX 16.0 // room around the thumbnail for its shadow
#define CARD_PADT 12.0
#define CARD_W (THUMB_W + 2 * CARD_PADX)
#define CARD_H 362.0 // the thumbnail and its shadow
#define CARD_X (THUMB_X - CARD_PADX) // the card window's origin in the stage
#define CARD_Y (THUMB_Y - CARD_PADT)
#define PLACE_ICON 128.0
#define INSET 32.0
#define HEADER_CY 32.0
#define DOT 7.0
#define DOT_GAP 8.0
#define PILL 22.0
#define DOTS_CY 403.5
#define MAX_DOTS 12
#define COUNTER_W 80.0
#define COUNTER_H 20.0
#define SQUARE_DX 120.0 // how far a card slides before it has faded out
#define SQUARE_FADE 1.6
#define MOVE_MS 340.0
#define POOL 2 // the selected card and the one that just left
#define CARD_RES 1 // the cards are drawn at 1x: 2x would add ~15MB at peak (the stage, the counter and the dots stay sharp)
#define HEADER_FONT 14

typedef struct {
    uint32_t wid;
    CGContextRef ctx;
    bool used;
    long v;
    double from, to; // position in slots, p
    CGImageRef img;  // the window's screenshot, only while it is being drawn
    bool shot;       // the screenshot is already in the card
} card_t;

static int cid;
static uint32_t stage, counter, dots;
static CGContextRef stage_ctx, counter_ctx, dots_ctx;
static card_t cards[POOL];
static sw_entry_t *ents;
static int n;
static long sel_v;
static int last_dir;
static bool thumbs, dark;
static palette_t pal;
static CTFontRef font;
static CGPoint so; // the stage's origin
static dispatch_source_t timer;
static double t0;

static int idx_of(long v) {
    return sel_wrap(v, n);
}

// fg at t over bg.
static uint32_t sw_mix(uint32_t fg, uint32_t bg, CGFloat t) {
    uint32_t out = 0;
    for (int sh = 16; sh >= 0; sh -= 8)
        out |= (uint32_t)lround(((fg >> sh & 255) * t) + ((bg >> sh & 255) * (1 - t))) << sh;
    return out;
}

static uint32_t dim_text(void) {
    return sw_mix(pal.title, pal.tint, 0.45);
}

static void draw_stage(void) {
    CGSize sz = {STAGE_W, STAGE_H};
    sw_begin(stage_ctx, sz);
    sw_fill_rrect(stage_ctx, (CGRect){{0, 0}, sz}, STAGE_R, pal.tint, pal.tint_a);
    sw_edge_rrect(stage_ctx, (CGRect){{0, 0}, sz}, STAGE_R, &pal);
    char buf[32];
    snprintf(buf, sizeof buf, "Desktop %d", wm_current());
    CFStringRef s = CFStringCreateWithCString(NULL, buf, kCFStringEncodingUTF8);
    sw_put(stage_ctx, sw_fit(font, s, dim_text(), STAGE_W / 2, kCTLineTruncationEnd), INSET, HEADER_CY);
    CFRelease(s);
    sw_finish(stage_ctx);
}

static void draw_counter(void) {
    CGSize sz = {COUNTER_W, COUNTER_H};
    sw_begin(counter_ctx, sz);
    char buf[32];
    snprintf(buf, sizeof buf, "%d / %d", idx_of(sel_v) + 1, n);
    CFStringRef s = CFStringCreateWithCString(NULL, buf, kCFStringEncodingUTF8);
    CTLineRef l = sw_fit(font, s, dim_text(), COUNTER_W, kCTLineTruncationEnd);
    CGFloat w = CTLineGetTypographicBounds(l, NULL, NULL, NULL);
    sw_put(counter_ctx, l, COUNTER_W - w, COUNTER_H / 2);
    CFRelease(s);
    sw_finish(counter_ctx);
}

static CGFloat dots_w(void) {
    return MAX_DOTS * (DOT + DOT_GAP) + PILL;
}

static void draw_dots(void) {
    CGSize sz = {dots_w(), DOT + 4};
    sw_begin(dots_ctx, sz);
    int nd = n < MAX_DOTS ? n : MAX_DOTS, at = idx_of(sel_v);
    int first = sel_window(at, n, nd);
    CGFloat x = (sz.width - ((nd - 1) * (DOT + DOT_GAP) + PILL)) / 2, y = (sz.height - DOT) / 2;
    for (int i = first; i < first + nd; i++) {
        bool on = i == at;
        CGFloat w = on ? PILL : DOT;
        sw_fill_rrect(dots_ctx, (CGRect){{x, y}, {w, DOT}}, DOT / 2, on ? pal.title : dim_text(), 1);
        x += w + DOT_GAP;
    }
    sw_finish(dots_ctx);
}

static void stroke_rrect(CGContextRef c, CGRect r, CGFloat radius, uint32_t rgb, CGFloat a) {
    CGPathRef p = CGPathCreateWithRoundedRect(CGRectInset(r, 0.5, 0.5), radius - 0.5, radius - 0.5, NULL);
    sw_color(c, rgb, a, true);
    CGContextSetLineWidth(c, 1);
    CGContextAddPath(c, p);
    CGContextStrokePath(c);
    CGPathRelease(p);
}

static void draw_image(CGContextRef c, CGImageRef img, CGRect r, bool round) {
    CGContextSaveGState(c);
    if (round) {
        CGPathRef p = CGPathCreateWithRoundedRect(r, THUMB_R, THUMB_R, NULL);
        CGContextAddPath(c, p);
        CGContextClip(c);
        CGPathRelease(p);
    }
    CGContextTranslateCTM(c, r.origin.x, r.origin.y + r.size.height);
    CGContextScaleCTM(c, 1, -1);
    CGContextDrawImage(c, (CGRect){{0, 0}, r.size}, img);
    CGContextRestoreGState(c);
}

// The thumbnail (or the app icon until the screenshot is there).
static void draw_card(card_t *k) {
    const sw_entry_t *e = &ents[idx_of(k->v)];
    CGContextRef c = k->ctx;
    CGSize sz = {CARD_W, CARD_H};
    sw_begin(c, sz);
    CGRect box = {{CARD_PADX, CARD_PADT}, {THUMB_W, THUMB_H}};
    if (k->img) {
        CGRect r = cardmath_fit(CGSizeMake(CGImageGetWidth(k->img), CGImageGetHeight(k->img)), box);
        CGContextSaveGState(c);
        CGColorRef sh = CGColorCreateGenericGray(0, 0.4);
        CGContextSetShadowWithColor(c, CGSizeMake(0, -6), 14, sh);
        CGColorRelease(sh);
        sw_fill_rrect(c, r, THUMB_R, 0x000000, 1);
        CGContextRestoreGState(c);
        draw_image(c, k->img, r, true);
        stroke_rrect(c, r, THUMB_R, 0xffffff, 0.12);
    } else {
        sw_fill_rrect(c, box, THUMB_R, dark ? 0xffffff : 0x000000, dark ? 0.06 : 0.05);
        stroke_rrect(c, box, THUMB_R, dark ? 0xffffff : 0x000000, 0.12);
        CGImageRef big = apps_icon(e->pid, (int)(PLACE_ICON * 2));
        if (big) {
            draw_image(c, big, (CGRect){{box.origin.x + (THUMB_W - PLACE_ICON) / 2, box.origin.y + (THUMB_H - PLACE_ICON) / 2},
                                        {PLACE_ICON, PLACE_ICON}}, false);
            CGImageRelease(big);
        }
    }
    sw_finish(c);
}

static void request_thumb(int ci) {
    if (!thumbs) return;
    long v = cards[ci].v;
    if (ents[idx_of(v)].nopreview) return;
    thumb_capture(ents[idx_of(v)].wid, ^(CGImageRef img) {
        card_t *k = &cards[ci];
        if (!img || !k->used || k->v != v || k->shot) return;
        k->img = CGImageRetain(img);
        draw_card(k);
        CGImageRelease(k->img);
        k->img = NULL;
        k->shot = true;
    });
}

static void set_alpha(card_t *k, double a) {
    SLSSetWindowAlpha(cid, k->wid, (float)a);
}

// Moves card k to p slots from the centre and applies the pose.
static void place(card_t *k, double p) {
    cardmath_pose_t q = cardmath_pose(p, SQUARE_DX, SQUARE_FADE);
    CGPoint o = {so.x + CARD_X + q.x, so.y + CARD_Y};
    SLSMoveWindow(cid, k->wid, &o);
    set_alpha(k, q.a);
}

static void release_card(card_t *k) {
    sw_close_window(cid, &k->wid, &k->ctx);
    if (k->img) CGImageRelease(k->img);
    *k = (card_t){0};
}

static void close_cards(void) {
    sw_timer_stop(&timer);
    thumb_end();
    for (int i = 0; i < POOL; i++) release_card(&cards[i]);
    sw_close_window(cid, &stage, &stage_ctx);
    sw_close_window(cid, &counter, &counter_ctx);
    sw_close_window(cid, &dots, &dots_ctx);
    if (font) CFRelease(font);
    font = NULL;
    sw_free_entries(ents, n);
    ents = NULL;
    n = 0;
    malloc_zone_pressure_relief(NULL, 0);
}

static void tick(void) {
    double k = (CFAbsoluteTimeGetCurrent() - t0) * 1000 / MOVE_MS;
    bool done = k >= 1;
    double e = done ? 1 : cardmath_ease(k);
    SLSDisableUpdate(cid);
    for (int i = 0; i < POOL; i++) {
        card_t *c = &cards[i];
        if (!c->used) continue;
        place(c, c->from + (c->to - c->from) * e);
        if (done) c->from = c->to;
    }
    SLSReenableUpdate(cid);
    if (done) sw_timer_stop(&timer);
}

static card_t *new_card(long v) {
    for (int i = 0; i < POOL; i++) {
        card_t *k = &cards[i];
        if (k->used) continue;
        if (!k->wid) {
            k->wid = sw_window(cid, so, (CGSize){CARD_W, CARD_H}, 0, CARD_RES, &k->ctx);
            if (!k->wid || !k->ctx) {
                release_card(k);
                return NULL;
            }
            set_alpha(k, 0);
        }
        k->shot = false;
        k->used = true;
        k->v = v;
        SLSOrderWindow(cid, k->wid, 1, stage);
        draw_card(k);
        request_thumb(i);
        return k;
    }
    return NULL;
}

// Hands every needed position a card (reusing the ones that left) and aims every card at its slot.
// A surviving card keeps its from; a new one starts one slot beyond its target, on the side it enters from.
static void settle(bool animate) {
    long need[2];
    int m = cardmath_needed(sel_v, last_dir, n, need);
    for (int i = 0; i < POOL; i++) {
        card_t *k = &cards[i];
        bool keep = false;
        for (int j = 0; j < m && k->used && !keep; j++) keep = need[j] == k->v;
        if (k->used && !keep) {
            k->used = false;
            if (k->wid) set_alpha(k, 0);
        }
    }
    for (int j = 0; j < m; j++) {
        bool held = false;
        for (int i = 0; i < POOL && !held; i++) held = cards[i].used && cards[i].v == need[j];
        if (held) continue;
        card_t *k = new_card(need[j]);
        if (k) k->from = (need[j] - sel_v) + last_dir;
    }
    for (int i = 0; i < POOL; i++) {
        card_t *k = &cards[i];
        if (!k->used) continue;
        k->to = k->v - sel_v;
        if (!animate) k->from = k->to;
        if (k->wid) SLSOrderWindow(cid, k->wid, 1, stage);
    }
}

static void open_cards(int dir) {
    int sel;
    n = wm_windows(dir, &ents, &sel);
    if (!n) {
        close_cards();
        return;
    }
    cid = SLSMainConnectionID();
    dark = appearance_dark();
    pal = *sw_palette();
    if (dark) pal.tint_a = 0.82;
    font = sw_font(HEADER_FONT);
    CGRect d = CGDisplayBounds(CGMainDisplayID());
    so = CGPointMake(d.origin.x + (d.size.width - STAGE_W) / 2, d.origin.y + (d.size.height - STAGE_H) / 2);
    if (font) {
        stage = sw_window(cid, so, (CGSize){STAGE_W, STAGE_H}, STAGE_R, 2, &stage_ctx);
        counter = sw_window(cid, (CGPoint){so.x + STAGE_W - INSET - COUNTER_W, so.y + HEADER_CY - COUNTER_H / 2},
                            (CGSize){COUNTER_W, COUNTER_H}, 0, 2, &counter_ctx);
        dots = sw_window(cid, (CGPoint){so.x + (STAGE_W - dots_w()) / 2, so.y + DOTS_CY - (DOT + 4) / 2},
                         (CGSize){dots_w(), DOT + 4}, 0, 2, &dots_ctx);
    }
    if (!font || !stage || !counter || !stage_ctx || !counter_ctx || !dots || !dots_ctx) {
        close_cards();
        return;
    }
    sel_v = sel;
    last_dir = 0;
    thumbs = thumb_ready();
    if (thumbs) thumb_begin();
    draw_stage();
    draw_counter();
    draw_dots();
    SLSOrderWindow(cid, stage, 1, 0);
    SLSOrderWindow(cid, counter, 1, stage);
    SLSOrderWindow(cid, dots, 1, stage);
    settle(false);
    for (int i = 0; i < POOL; i++)
        if (cards[i].used) place(&cards[i], cards[i].to);
}

void card_step(int dir) {
    if (!stage) {
        open_cards(dir);
        return;
    }
    if (n < 2) return;
    double k = timer ? (CFAbsoluteTimeGetCurrent() - t0) * 1000 / MOVE_MS : 1;
    double e = k >= 1 ? 1 : cardmath_ease(k);
    for (int i = 0; i < POOL; i++) {
        card_t *c = &cards[i];
        if (c->used) c->from += (c->to - c->from) * e;
    }
    sel_v += dir;
    last_dir = dir;
    draw_counter();
    draw_dots();
    settle(true);
    SLSDisableUpdate(cid);
    for (int i = 0; i < POOL; i++)
        if (cards[i].used) place(&cards[i], cards[i].from);
    SLSReenableUpdate(cid);
    t0 = CFAbsoluteTimeGetCurrent();
    sw_timer_start(&timer, tick);
}

void card_commit(void) {
    if (!stage) return;
    uint32_t target = ents[idx_of(sel_v)].wid;
    close_cards();
    wm_focus_wid(target);
}

void card_cancel(void) {
    if (stage) close_cards();
}
