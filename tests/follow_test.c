#include "../src/follow.h"
#include <stdio.h>

static int failed;

static const char *name(focus_act_t a) {
    static const char *n[] = {"follow", "refocus", "stale", "ignore"};
    return n[a];
}

int main(void) {
    static const struct {
        const char *what;
        bool settling, recheck, is_stale, cur_has_win, user_call;
        focus_act_t want;
    } t[] = {
        {"parked window called for", false, false, false, true, false, FOCUS_FOLLOW},
        {"parked window called for, desktop empty", false, false, false, false, false, FOCUS_FOLLOW},
        {"parked window while settling", true, false, false, true, false, FOCUS_IGNORE},
        {"parked window while settling, at recheck", true, true, false, true, false, FOCUS_IGNORE},
        {"the window just parked", false, false, true, true, false, FOCUS_IGNORE},
        {"the window just parked, at recheck", false, true, true, true, false, FOCUS_IGNORE},
        {"parked window at recheck", false, true, false, true, false, FOCUS_REFOCUS},
        {"parked window at recheck, desktop empty", false, true, false, false, false, FOCUS_STALE},
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        focus_act_t got = focus_act(t[i].settling, t[i].recheck, t[i].is_stale, t[i].cur_has_win, t[i].user_call);
        if (got == t[i].want) continue;
        printf("FAIL %s = %s; want %s\n", t[i].what, name(got), name(t[i].want));
        failed++;
    }
    static const struct {
        const char *what;
        bool still_focused, now_here, settling, cur_left, user_call;
        follow_act_t want;
    } c[] = {
        {"call still stands", true, false, false, false, false, FOLLOW_SWITCH},
        {"focus moved on", false, false, false, false, false, FOLLOW_DROP},
        {"focus moved on, window left", false, false, false, true, false, FOLLOW_DROP},
        {"window is now here", true, true, false, false, false, FOLLOW_DROP},
        {"settling started meanwhile", true, false, true, false, false, FOLLOW_WAIT},
        {"settling started meanwhile, window left", true, false, true, true, false, FOLLOW_WAIT},
        {"settling started, focus moved on", false, false, true, false, false, FOLLOW_DROP},
        {"settling started, window is now here", true, true, true, false, false, FOLLOW_DROP},
        {"window here was minimized, closed or quit", true, false, false, true, false, FOLLOW_LEFT},
        {"user call while settling", true, false, true, false, true, FOLLOW_SWITCH},
        {"user call, focus moved on", false, false, true, false, true, FOLLOW_DROP},
        {"user call, window is now here", true, true, true, false, true, FOLLOW_DROP},
    };
    static const char *const an[] = {"drop", "wait", "left", "switch"};
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        follow_act_t got = follow_check(c[i].still_focused, c[i].now_here, c[i].settling, c[i].cur_left, c[i].user_call);
        if (got == c[i].want) continue;
        printf("FAIL check: %s = %s; want %s\n", c[i].what, an[got], an[c[i].want]);
        failed++;
    }
    static const struct {
        const char *what;
        bool tracked_here, known_minimized, alive;
        ax_read_t read;
        bool ax_minimized, want;
    } l[] = {
        {"window is fine", true, false, true, READ_OK, false, false},
        {"minimize in flight", true, false, true, READ_OK, true, true},
        {"close in flight", true, false, true, READ_INVALID, false, true},
        {"quit in flight", true, false, false, READ_OK, false, true},
        {"app busy or quitting", true, false, true, READ_BUSY, false, true},
        {"other read error", true, false, true, READ_OTHER, false, false},
        {"already minimized", true, true, true, READ_OK, true, false},
        {"already minimized, app quit since", true, true, false, READ_OK, true, false},
        {"already removed", false, false, false, READ_OTHER, false, false},
        {"moved to another desktop", false, false, true, READ_OK, true, false},
        {"never had a window here", false, false, true, READ_OTHER, false, false},
    };
    for (unsigned i = 0; i < sizeof l / sizeof l[0]; i++) {
        bool got = cur_left(l[i].tracked_here, l[i].known_minimized, l[i].alive, l[i].read, l[i].ax_minimized);
        if (got == l[i].want) continue;
        printf("FAIL cur_left: %s = %d; want %d\n", l[i].what, got, l[i].want);
        failed++;
    }
    static const struct {
        const char *what;
        double at, settle_start, now;
        bool want;
    } k[] = {
        {"no input", 0, 100, 101, false},
        {"input right after the quiet period began", 100.5, 100, 101, true},
        {"input before the quiet period (the switch key itself)", 99.9, 100, 101, false},
        {"input exactly at the start", 100, 100, 101, false},
        {"input too long ago", 100.5, 100, 101.6, false},
        {"input at the edge of the window", 100.5, 100, 101.5, true},
        {"input in the future", 102, 100, 101, false},
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++) {
        bool got = input_is_call(k[i].at, k[i].settle_start, k[i].now);
        if (got == k[i].want) continue;
        printf("FAIL input_is_call: %s = %d; want %d\n", k[i].what, got, k[i].want);
        failed++;
    }
    static const struct {
        const char *what;
        double key_at, click_at, now;
        bool want;
    } q[] = {
        {"fresh key", 100.5, 0, 101, true},
        {"no key", 0, 0, 101, false},
        {"click after the key", 100.5, 100.7, 101, false},
        {"click before the key", 100.5, 100.2, 101, true},
        {"key too long ago", 100, 0, 101.5, false},
        {"key at the edge of the window", 100, 0, 101, true},
        {"key in the future", 102, 0, 101, false},
    };
    for (unsigned i = 0; i < sizeof q / sizeof q[0]; i++) {
        bool got = input_is_quick_call(q[i].key_at, q[i].click_at, q[i].now);
        if (got == q[i].want) continue;
        printf("FAIL input_is_quick_call: %s = %d; want %d\n", q[i].what, got, q[i].want);
        failed++;
    }
    if (!failed) printf("ok\n");
    return failed != 0;
}
