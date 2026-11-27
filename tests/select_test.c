#include "../src/select.h"
#include <stdio.h>

static int failed;

static void eq(const char *what, int got, int want) {
    if (got == want) return;
    printf("FAIL %s = %d; want %d\n", what, got, want);
    failed++;
}

static void test_wrap(void) {
    eq("sel_wrap(0, 5)", sel_wrap(0, 5), 0);
    eq("sel_wrap(4, 5)", sel_wrap(4, 5), 4);
    eq("sel_wrap(5, 5)", sel_wrap(5, 5), 0);
    eq("sel_wrap(-1, 5)", sel_wrap(-1, 5), 4);
    eq("sel_wrap(-5, 5)", sel_wrap(-5, 5), 0);
    eq("sel_wrap(-6, 5)", sel_wrap(-6, 5), 4);
    eq("sel_wrap(7, 3)", sel_wrap(7, 3), 1);
    eq("sel_wrap(-7, 3)", sel_wrap(-7, 3), 2);
    eq("sel_wrap(any, 1)", sel_wrap(-123456789L, 1), 0);
    eq("sel_wrap(unwrapped strip position)", sel_wrap(3000000001L, 7), (int)(3000000001L % 7));
}

static void test_start(void) {
    eq("sel_start(none focused, forward)", sel_start(-1, 1, 4), 0);
    eq("sel_start(none focused, backward)", sel_start(-1, -1, 4), 3);
    eq("sel_start(focused 1, forward)", sel_start(1, 1, 4), 2);
    eq("sel_start(focused 1, backward)", sel_start(1, -1, 4), 0);
    eq("sel_start(last focused, forward wraps)", sel_start(3, 1, 4), 0);
    eq("sel_start(first focused, backward wraps)", sel_start(0, -1, 4), 3);
    eq("sel_start(only window, forward)", sel_start(0, 1, 1), 0);
    eq("sel_start(only window, backward)", sel_start(0, -1, 1), 0);
    eq("sel_start(empty list)", sel_start(-1, 1, 0), 0);
    eq("sel_start(empty list, backward)", sel_start(-1, -1, 0), 0);
}

static void test_top(void) {
    eq("sel_top(middle row of the view)", sel_top(2, 3, 3), 2);
    eq("sel_top(first row of the view)", sel_top(2, 2, 3), 2);
    eq("sel_top(last row of the view)", sel_top(2, 4, 3), 2);
    eq("sel_top(one past the view)", sel_top(2, 5, 3), 3);
    eq("sel_top(far past the view)", sel_top(0, 20, 9), 12);
    eq("sel_top(above the view)", sel_top(5, 1, 3), 1);
    eq("sel_top(wrapped to the start)", sel_top(7, 0, 9), 0);
    eq("sel_top(first open, selection inside)", sel_top(0, 3, 9), 0);
    eq("sel_top(first open, selection beyond)", sel_top(0, 12, 9), 4);
}

static void test_window(void) {
    eq("sel_window(all dots fit)", sel_window(4, 5, 5), 0);
    eq("sel_window(single dot)", sel_window(0, 1, 1), 0);
    eq("sel_window(at the start)", sel_window(0, 20, 12), 0);
    eq("sel_window(still near the start)", sel_window(5, 20, 12), 0);
    eq("sel_window(centred)", sel_window(10, 20, 12), 4);
    eq("sel_window(near the end)", sel_window(18, 20, 12), 8);
    eq("sel_window(at the end)", sel_window(19, 20, 12), 8);
}

static void test_row_at(void) {
    eq("sel_row_at(above the first row)", sel_row_at(-0.5, 28, 0, 5), -1);
    eq("sel_row_at(first row)", sel_row_at(0, 28, 0, 5), 0);
    eq("sel_row_at(inside the second row)", sel_row_at(40, 28, 0, 5), 1);
    eq("sel_row_at(last row)", sel_row_at(139, 28, 0, 5), 4);
    eq("sel_row_at(below the last row)", sel_row_at(140, 28, 0, 5), -1);
    eq("sel_row_at(scrolled)", sel_row_at(30, 28, 6, 5), 7);
}

static void test_clamp(void) {
    eq("sel_clamp(below)", sel_clamp(-3, 0, 4), 0);
    eq("sel_clamp(inside)", sel_clamp(2, 0, 4), 2);
    eq("sel_clamp(above)", sel_clamp(9, 0, 4), 4);
}

int main(void) {
    test_wrap();
    test_start();
    test_top();
    test_window();
    test_row_at();
    test_clamp();
    puts(failed ? "FAILED" : "ok");
    return failed != 0;
}
