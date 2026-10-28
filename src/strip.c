#include "strip.h"

int strip_slots(int n, int over) {
    return n + (over > 0);
}

int strip_width(int slots) {
    return (slots + 1) * STRIP_SLOT + slots * STRIP_GAP;
}

int strip_reserve(int need, int reserve, bool expired) {
    if (need < STRIP_MIN_SLOTS) need = STRIP_MIN_SLOTS;
    return need >= reserve || expired ? need : reserve;
}

static int slot_at(double x, int n) {
    if (x < 0) return -1;
    int i = (int)(x / (STRIP_SLOT + STRIP_GAP));
    return i < n && x - i * (STRIP_SLOT + STRIP_GAP) < STRIP_SLOT ? i : -1;
}

// The image is centred in the button; the strip is right-aligned in the image.
static double strip_left(double button_w, int reserve, int n, int over) {
    return (button_w + strip_width(reserve)) / 2 - strip_width(strip_slots(n, over));
}

strip_hit_t strip_hit(double x, double button_w, int reserve, int n, int over) {
    double sx = x - strip_left(button_w, reserve, n, over);
    return (strip_hit_t){slot_at(sx, n), over > 0 && slot_at(sx, n + 1) == n};
}

double strip_overflow_left(double button_w, int reserve, int n, int over) {
    return strip_left(button_w, reserve, n, over) + n * (STRIP_SLOT + STRIP_GAP);
}
