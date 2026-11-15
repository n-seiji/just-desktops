#include "shown.h"
#include <math.h>

bool shown_visible(double alpha, CGRect r, const CGRect *displays, uint32_t nd) {
    if (alpha <= 0) return false;
    for (uint32_t k = 0; k < nd; k++) {
        CGRect v = CGRectIntersection(r, displays[k]);
        if (!CGRectIsNull(v) && r.size.width > 0 && r.size.height > 0 &&
            v.size.width >= fmin(r.size.width, 32) && v.size.height >= fmin(r.size.height, 32))
            return true;
    }
    return false;
}

bool shown_grace(bool listed, bool *was_listed, double now, double shown_at) {
    if (now - shown_at > FRESH_S) {
        *was_listed = listed;
        return listed;
    }
    return listed || *was_listed;
}

uint32_t shown_front(double now, double fresh_until, uint32_t fresh_wid, uint32_t focused_wid) {
    return now < fresh_until && fresh_wid ? fresh_wid : focused_wid;
}

CGRect shown_park_display(const CGRect *displays, uint32_t n) {
    if (n == 0) return CGRectZero;
    CGRect best = displays[0];
    for (uint32_t k = 1; k < n; k++) {
        CGRect d = displays[k];
        if (CGRectGetMaxX(d) > CGRectGetMaxX(best) ||
            (CGRectGetMaxX(d) == CGRectGetMaxX(best) && CGRectGetMaxY(d) > CGRectGetMaxY(best)))
            best = d;
    }
    return best;
}

CGPoint shown_park_point(CGRect display) {
    return CGPointMake(CGRectGetMaxX(display) - 1, CGRectGetMaxY(display) - 1);
}

bool shown_is_parked(CGPoint p, CGRect display) {
    CGPoint h = shown_park_point(display);
    return p.x >= h.x - 32 && p.x <= h.x + 4 && p.y >= h.y - 96 && p.y <= h.y + 4;
}

CGPoint shown_safe_point(CGRect display) {
    return CGPointMake(display.origin.x + 40, display.origin.y + 60);
}

CGPoint shown_return_point(CGPoint read, CGRect park, CGRect main_display, bool has_saved, CGPoint saved, double since_shown) {
    if (shown_is_parked(read, park)) return has_saved ? saved : shown_safe_point(main_display);
    if (has_saved && since_shown >= 0 && since_shown <= FRESH_S) return saved;
    return read;
}

bool shown_missed(CGPoint want, CGPoint got) {
    return fabs(want.x - got.x) > 1 || fabs(want.y - got.y) > 1;
}
