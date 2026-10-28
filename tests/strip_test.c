#include "../src/strip.h"
#include <stdio.h>

static int failed;

static void eq(const char *what, double got, double want) {
    if (got == want) return;
    printf("FAIL %s = %g; want %g\n", what, got, want);
    failed++;
}

static void hit(const char *what, double x, double button_w, int reserve, int n, int over, int slot, bool overflow) {
    strip_hit_t h = strip_hit(x, button_w, reserve, n, over);
    if (h.slot == slot && h.overflow == overflow) return;
    printf("FAIL strip_hit(%s) = {slot %d, overflow %d}; want {slot %d, overflow %d}\n", what, h.slot, h.overflow, slot, overflow);
    failed++;
}

int main(void) {
    eq("strip_slots(3, 0)", strip_slots(3, 0), 3);
    eq("strip_slots(9, 4)", strip_slots(9, 4), 10);
    eq("strip_width(0): the key box alone", strip_width(0), 20);
    eq("strip_width(1)", strip_width(1), 44);
    eq("strip_width(10)", strip_width(10), 11 * 20 + 10 * 4);

    eq("STRIP_MIN_SLOTS", STRIP_MIN_SLOTS, 5);
    eq("STRIP_SHRINK_S", STRIP_SHRINK_S, 30);

    // The reserve: grows at once, shrinks to max(need, minimum) only once the timer has fired.
    eq("reserve: few slots keep the minimum", strip_reserve(3, 5, false), 5);
    eq("reserve: few slots, timer fired", strip_reserve(3, 5, true), 5);
    eq("reserve: no windows", strip_reserve(0, 0, false), 5);
    eq("reserve: exactly the minimum", strip_reserve(5, 5, false), 5);
    eq("reserve: grows at once", strip_reserve(7, 5, false), 7);
    eq("reserve: grows from nothing", strip_reserve(8, 0, false), 8);
    eq("reserve: holds while the timer runs", strip_reserve(6, 9, false), 9);
    eq("reserve: shrinks to the need", strip_reserve(6, 9, true), 6);
    eq("reserve: shrinks to the minimum", strip_reserve(2, 9, true), 5);
    eq("reserve: need equals reserve", strip_reserve(9, 9, true), 9);

    // One glyph, 5 reserved: the image is 140pt, centred in a 160pt button (starts at 10); the content
    // is 44pt, right-aligned in the image, so it starts at 106.
    hit("empty space on the left", 105, 160, 5, 1, 0, -1, false);
    hit("first pixel of the glyph", 106, 160, 5, 1, 0, 0, false);
    hit("middle of the glyph", 116, 160, 5, 1, 0, 0, false);
    hit("last pixel of the glyph", 125.9, 160, 5, 1, 0, 0, false);
    hit("gap after the glyph", 126, 160, 5, 1, 0, -1, false);
    hit("key box", 140, 160, 5, 1, 0, -1, false);
    hit("right of the strip", 155, 160, 5, 1, 0, -1, false);
    hit("negative x", -5, 160, 5, 1, 0, -1, false);

    // Two glyphs and a "+3" slot, 5 reserved: content is 92pt, starting at 58 (slots at 58, 82, 106).
    hit("first of two glyphs", 60, 160, 5, 2, 3, 0, false);
    hit("second glyph", 90, 160, 5, 2, 3, 1, false);
    hit("gap before the +k slot", 104, 160, 5, 2, 3, -1, false);
    hit("+k slot", 115, 160, 5, 2, 3, -1, true);
    hit("gap after the +k slot", 128, 160, 5, 2, 3, -1, false);
    hit("key box after +k", 140, 160, 5, 2, 3, -1, false);
    eq("strip_overflow_left", strip_overflow_left(160, 5, 2, 3), 106);

    // Content that fills the reserve starts at the image's left edge: 7 slots in a 188pt image, 200pt button.
    hit("full strip, left of the image", 5, 200, 7, 7, 0, -1, false);
    hit("full strip, first glyph", 6, 200, 7, 7, 0, 0, false);
    eq("full strip, overflow_left", strip_overflow_left(200, 7, 6, 1), 6 + 6 * 24);

    // Without overflow the slot after the glyphs is the key box (2 glyphs: content 68pt, starts at 82).
    hit("key box is not +k", 140, 160, 5, 2, 0, -1, false);

    puts(failed ? "FAILED" : "ok");
    return failed != 0;
}
