#include "../src/shown.h"
#include <stdio.h>

static int failed;

static void visible(const char *what, double alpha, CGRect r, const CGRect *d, uint32_t nd, bool want) {
    bool got = shown_visible(alpha, r, d, nd);
    if (got == want) return;
    printf("FAIL shown_visible(%s) = %d; want %d\n", what, got, want);
    failed++;
}

static void grace(const char *what, bool listed, bool was, double now, double shown_at, bool want, bool want_was) {
    bool w = was;
    bool got = shown_grace(listed, &w, now, shown_at);
    if (got == want && w == want_was) return;
    printf("FAIL shown_grace(%s) = %d, was_listed %d; want %d, %d\n", what, got, w, want, want_was);
    failed++;
}

static void parked(const char *what, CGPoint p, bool want) {
    CGRect main_display = {{0, 0}, {1920, 1212}};
    bool got = shown_is_parked(p, main_display);
    if (got == want) return;
    printf("FAIL shown_is_parked(%s) = %d; want %d\n", what, got, want);
    failed++;
}

static void front(const char *what, double now, double until, uint32_t fresh, uint32_t focused, uint32_t want) {
    uint32_t got = shown_front(now, until, fresh, focused);
    if (got == want) return;
    printf("FAIL shown_front(%s) = %u; want %u\n", what, got, want);
    failed++;
}

static void same_point(const char *what, CGPoint got, CGPoint want) {
    if (got.x == want.x && got.y == want.y) return;
    printf("FAIL %s = {%g,%g}; want {%g,%g}\n", what, got.x, got.y, want.x, want.y);
    failed++;
}

static void missed(const char *what, CGPoint want, CGPoint got, bool expect) {
    bool m = shown_missed(want, got);
    if (m == expect) return;
    printf("FAIL shown_missed(%s) = %d; want %d\n", what, m, expect);
    failed++;
}

static void park_display(const char *what, const CGRect *d, uint32_t n, CGRect want) {
    CGRect got = shown_park_display(d, n);
    if (CGRectEqualToRect(got, want)) return;
    printf("FAIL shown_park_display(%s) = {%g,%g %gx%g}; want {%g,%g %gx%g}\n", what, got.origin.x, got.origin.y,
           got.size.width, got.size.height, want.origin.x, want.origin.y, want.size.width, want.size.height);
    failed++;
}

// A window parked for these displays is off all of them.
static void park_off_screen(const char *what, const CGRect *d, uint32_t n) {
    CGPoint p = shown_park_point(shown_park_display(d, n));
    visible(what, 1, (CGRect){p, {800, 600}}, d, n, false);
}

static void park_arrangements(void) {
    CGRect main_d = {{0, 0}, {1920, 1212}};
    CGRect right = {{1920, 0}, {1280, 800}};
    CGRect left = {{-1280, 0}, {1280, 800}};
    CGRect above = {{0, -800}, {1920, 800}};
    CGRect below = {{0, 1212}, {1920, 800}};
    CGRect short_right = {{1920, 0}, {1280, 600}};
    CGRect far_right = {{3200, 100}, {1000, 700}};

    CGRect single[] = {main_d};
    CGRect r2[] = {main_d, right};
    CGRect l2[] = {main_d, left};
    CGRect a2[] = {main_d, above};
    CGRect b2[] = {main_d, below};
    CGRect s2[] = {main_d, short_right};
    CGRect r3[] = {main_d, right, far_right};
    CGRect r3b[] = {far_right, left, main_d};

    park_display("single display", single, 1, main_d);
    park_display("second display to the right", r2, 2, right);
    park_display("second display to the left", l2, 2, main_d);
    park_display("second display above", a2, 2, main_d);
    park_display("second display below, same maxX", b2, 2, below);
    park_display("to the right but shorter, top-aligned", s2, 2, short_right);
    park_display("three displays", r3, 3, far_right);
    park_display("three displays, other order", r3b, 3, far_right);
    CGRect low_first[] = {below, main_d};
    park_display("same maxX, the lower one listed first", low_first, 2, below);
    CGRect up_right = {{1920, -900}, {1600, 900}}, left_neg = {{-1280, 200}, {1280, 800}};
    CGRect neg[] = {left_neg, main_d, up_right};
    park_display("negative origins: left of and above-right of main", neg, 3, up_right);
    CGRect twins[] = {main_d, main_d};
    park_display("mirrored: two equal rects", twins, 2, main_d);
    park_display("no displays", single, 0, CGRectZero);

    park_off_screen("single display", single, 1);
    park_off_screen("second display to the right", r2, 2);
    park_off_screen("second display to the left", l2, 2);
    park_off_screen("second display above", a2, 2);
    park_off_screen("second display below", b2, 2);
    park_off_screen("shorter display to the right", s2, 2);
    park_off_screen("three displays", r3, 3);
    park_off_screen("three displays, other order", r3b, 3);
}

int main(void) {
    CGRect main_display = {{0, 0}, {1920, 1212}};
    CGRect second = {{1920, 0}, {1280, 800}};
    CGRect both[] = {main_display, second};

    visible("inside", 1, (CGRect){{100, 100}, {800, 600}}, &main_display, 1, true);
    visible("transparent", 0, (CGRect){{100, 100}, {800, 600}}, &main_display, 1, false);
    visible("parked at the corner", 1, (CGRect){{1919, 1211}, {1885, 1192}}, &main_display, 1, false);
    visible("32pt still on the display", 1, (CGRect){{1888, 100}, {800, 600}}, &main_display, 1, true);
    visible("31pt on the display", 1, (CGRect){{1889, 100}, {800, 600}}, &main_display, 1, false);
    visible("small window fully inside", 1, (CGRect){{10, 10}, {10, 10}}, &main_display, 1, true);
    visible("small window half off", 1, (CGRect){{1915, 10}, {10, 10}}, &main_display, 1, false);
    visible("empty window", 1, (CGRect){{10, 10}, {0, 0}}, &main_display, 1, false);
    visible("on the second display", 1, (CGRect){{2000, 100}, {400, 300}}, both, 2, true);
    visible("on the second display only", 1, (CGRect){{2000, 100}, {400, 300}}, &main_display, 1, false);
    visible("no displays", 1, (CGRect){{100, 100}, {800, 600}}, both, 0, false);

    grace("long unparked, listed", true, false, 10, 1, true, true);
    grace("long unparked, not listed", false, true, 10, 1, false, false);
    grace("just unparked, not listed yet", false, true, 1.2, 1, true, true);
    grace("just unparked, not listed and never was", false, false, 1.2, 1, false, false);
    grace("just unparked, listed", true, false, 1.2, 1, true, false);
    grace("exactly at the end of the period", false, true, 0.3, 0, true, true);
    grace("just past the period", false, true, 0.31, 0, false, false);

    front("just focused by us", 5, 5.3, 42, 7, 42);
    front("period over", 5.3, 5.3, 42, 7, 7);
    front("nothing focused by us", 5, 5.3, 0, 7, 7);
    front("nothing focused at all", 6, 5.3, 42, 0, 0);

    CGPoint park = shown_park_point(main_display);
    if (park.x != 1919 || park.y != 1211) {
        printf("FAIL shown_park_point = {%g,%g}; want {1919,1211}\n", park.x, park.y);
        failed++;
    }
    parked("the park point", (CGPoint){1919, 1211}, true);
    parked("17pt left of it, as iPhone Mirroring stays", (CGPoint){1902, 1211}, true);
    parked("32pt left of it", (CGPoint){1887, 1211}, true);
    parked("33pt left of it", (CGPoint){1886, 1211}, false);
    parked("where macOS leaves it, 41pt up", (CGPoint){1919, 1170}, true);
    parked("64pt above it", (CGPoint){1919, 1147}, true);
    parked("65pt above it", (CGPoint){1919, 1146}, false);
    parked("a normal window", (CGPoint){100, 100}, false);
    parked("4pt right of it", (CGPoint){1923, 1211}, true);
    parked("5pt right of it, on a display to the right", (CGPoint){1924, 1211}, false);
    parked("4pt below it", (CGPoint){1919, 1215}, true);
    parked("5pt below it", (CGPoint){1919, 1216}, false);
    parked("past the corner, on a display to the lower right", (CGPoint){2500, 1500}, false);

    same_point("shown_safe_point", shown_safe_point(main_display), (CGPoint){40, 60});
    same_point("shown_safe_point, second display", shown_safe_point(second), (CGPoint){1960, 60});

    CGPoint at = {300, 200}, old = {50, 70}, corner = {1919, 1211};
    CGPoint safe = {40, 60};
    same_point("return: in the corner, has a place", shown_return_point(corner, main_display, main_display, true, old, 99), old);
    same_point("return: in the corner, fresh, has a place", shown_return_point(corner, main_display, main_display, true, old, 0.1), old);
    same_point("return: in the corner, no place", shown_return_point(corner, main_display, main_display, false, old, 99), safe);
    same_point("return: in the corner, no place, fresh", shown_return_point(corner, main_display, main_display, false, old, 0.1), safe);
    same_point("return: fresh, has a place", shown_return_point(at, main_display, main_display, true, old, 0.1), old);
    same_point("return: unparked just now", shown_return_point(at, main_display, main_display, true, old, 0), old);
    same_point("return: exactly FRESH_S", shown_return_point(at, main_display, main_display, true, old, FRESH_S), old);
    same_point("return: just over FRESH_S", shown_return_point(at, main_display, main_display, true, old, FRESH_S + 0.001), at);
    same_point("return: long ago", shown_return_point(at, main_display, main_display, true, old, 99), at);
    same_point("return: fresh, no place", shown_return_point(at, main_display, main_display, false, old, 0.1), at);
    same_point("return: never unparked", shown_return_point(at, main_display, main_display, false, old, 1e12), at);
    same_point("return: negative since_shown", shown_return_point(at, main_display, main_display, true, old, -1), at);

    // The window is parked at the park display's corner, the safe point is on the main display.
    CGRect far_display = {{1920, 0}, {1280, 800}};
    same_point("return: parked at another display's corner, no place", shown_return_point((CGPoint){3199, 799}, far_display, main_display, false, old, 99), safe);
    same_point("return: the main display's corner is not parked there", shown_return_point(corner, far_display, main_display, false, old, 99), corner);

    CGPoint past = {2500, 1500};
    same_point("return: past the corner, on a display to the lower right", shown_return_point(past, main_display, main_display, true, old, 99), past);
    same_point("return: never unparked, has a place", shown_return_point(at, main_display, main_display, true, old, 1e12), at);

    missed("same point", (CGPoint){100, 100}, (CGPoint){100, 100}, false);
    missed("1pt off in x", (CGPoint){100, 100}, (CGPoint){101, 100}, false);
    missed("1pt off in x, other way", (CGPoint){100, 100}, (CGPoint){99, 100}, false);
    missed("1.5pt off in x", (CGPoint){100, 100}, (CGPoint){101.5, 100}, true);
    missed("1.5pt off in x, other way", (CGPoint){100, 100}, (CGPoint){98.5, 100}, true);
    missed("1pt off in y", (CGPoint){100, 100}, (CGPoint){100, 101}, false);
    missed("1pt off in y, other way", (CGPoint){100, 100}, (CGPoint){100, 99}, false);
    missed("1.5pt off in y", (CGPoint){100, 100}, (CGPoint){100, 101.5}, true);
    missed("1.5pt off in y, other way", (CGPoint){100, 100}, (CGPoint){100, 98.5}, true);
    missed("40pt up", (CGPoint){100, 1242}, (CGPoint){100, 1202}, true);

    park_arrangements();

    puts(failed ? "FAILED" : "ok");
    return failed != 0;
}
