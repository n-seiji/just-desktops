#include "../src/keyrules.h"
#include <Carbon/Carbon.h> // the kVK_ key codes, so the expected keys do not come from src/
#include <stdio.h>

static int failed;

#define ALT kCGEventFlagMaskAlternate
#define SHIFT kCGEventFlagMaskShift
#define CMD kCGEventFlagMaskCommand
#define CTRL kCGEventFlagMaskControl

static void mods(const char *what, CGEventFlags flags, CGEventFlags base, mods_t want) {
    mods_t got = keyrules_mods(flags, base);
    if (got == want) return;
    printf("FAIL keyrules_mods(%s) = %d; want %d\n", what, got, want);
    failed++;
}

static void allowed(const char *what, bool repeat, bool pending, uint64_t now_ms, uint64_t last_ms, bool want) {
    bool got = keyrules_step_allowed(repeat, pending, now_ms * 1000000ull, last_ms * 1000000ull);
    if (got == want) return;
    printf("FAIL keyrules_step_allowed(%s) = %d; want %d\n", what, got, want);
    failed++;
}

static void call(const char *what, int64_t key, CGEventFlags flags, bool want) {
    bool got = keyrules_is_call_key(key, flags);
    if (got == want) return;
    printf("FAIL keyrules_is_call_key(%s) = %d; want %d\n", what, got, want);
    failed++;
}

static void quick(const char *what, int64_t key, CGEventFlags flags, bool want) {
    bool got = keyrules_is_quick_key(key, flags);
    if (got == want) return;
    printf("FAIL keyrules_is_quick_key(%s) = %d; want %d\n", what, got, want);
    failed++;
}

// keyrules_decide: one row per rule of the tap. A row gives the state and event going in and the decision and state
// coming out. Stamps start at call 7 and quick 8 so a changed, reset or untouched stamp each show.
#define NOW 500.0
#define MS 1000000ull

typedef struct {
    const char *name;
    CGEventFlags base; // 0: ALT
    const int64_t *keys; // NULL: DESK
    int nkeys;
    key_state_t in;
    key_event_t ev;
    key_decision_t want;
    key_state_t out;
} row_t;

static const int64_t DESK[] = {kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_Q, kVK_ANSI_W, kVK_ANSI_E};
static const int64_t WITH_BRACKET[] = {kVK_ANSI_1, kVK_ANSI_LeftBracket};

// ST(armed, step_pending, last_step in ms, last_call_key, last_quick_key)
#define ST(a, p, ms, call, quick) ((key_state_t){a, p, (ms) * MS, call, quick})
#define DOWN(k, f) ((key_event_t){KEYEV_DOWN, k, false, f, NOW, 1000 * MS})
#define REP(k, f) ((key_event_t){KEYEV_DOWN, k, true, f, NOW, 1000 * MS})
#define REP_AT(k, f, ms) ((key_event_t){KEYEV_DOWN, k, true, f, NOW, (ms) * MS})
#define FLAGS_EV(f) ((key_event_t){KEYEV_FLAGS, 0, false, f, NOW, 1000 * MS})
#define TAP_OFF(f) ((key_event_t){KEYEV_TAP_OFF, 0, false, f, NOW, 1000 * MS})
// D(swallow, cancel, action, arg)
#define D(s, c, a, g) ((key_decision_t){s, c, a, g})
#define PASS D(false, false, KEY_ACT_NONE, 0)
#define IDLE ST(false, false, 0, 7, 8)
#define ARMED ST(true, false, 0, 7, 8)

static void decide(const row_t *r) {
    key_state_t s = r->in;
    const int64_t *keys = r->keys ? r->keys : DESK;
    int nkeys = r->keys ? r->nkeys : 6;
    key_decision_t got = keyrules_decide(&s, &r->ev, r->base ? r->base : ALT, keys, nkeys);
    const key_decision_t *w = &r->want;
    const key_state_t *o = &r->out;
    if (got.swallow != w->swallow || got.cancel != w->cancel || (int)got.action != (int)w->action || got.arg != w->arg) {
        printf("FAIL keyrules_decide(%s) = {swallow %d, cancel %d, action %d, arg %d}; want {%d, %d, %d, %d}\n", r->name,
               got.swallow, got.cancel, got.action, got.arg, w->swallow, w->cancel, w->action, w->arg);
        failed++;
    }
    if (s.armed != o->armed || s.step_pending != o->step_pending || s.last_step_ns != o->last_step_ns ||
        s.last_call_key != o->last_call_key || s.last_quick_key != o->last_quick_key) {
        printf("FAIL keyrules_decide(%s) state = {armed %d, pending %d, step %llu, call %g, quick %g}; want {%d, %d, %llu, %g, %g}\n",
               r->name, s.armed, s.step_pending, s.last_step_ns, s.last_call_key, s.last_quick_key, o->armed,
               o->step_pending, o->last_step_ns, o->last_call_key, o->last_quick_key);
        failed++;
    }
}

static void decide_all(void) {
    const row_t rows[] = {
        // The tap came back on: the session's flags, the release may have been missed.
        {"tap off, armed, base released", 0, 0, 0, ARMED, TAP_OFF(0), D(false, false, KEY_ACT_COMMIT, 0), IDLE},
        {"tap off, armed, base held", 0, 0, 0, ARMED, TAP_OFF(ALT), PASS, ARMED},
        {"tap off, armed, base held with shift", 0, 0, 0, ARMED, TAP_OFF(ALT | SHIFT), PASS, ARMED},
        {"tap off, not armed", 0, 0, 0, IDLE, TAP_OFF(0), PASS, IDLE},
        {"tap off, cmd base, armed, only alt held", CMD, 0, 0, ARMED, TAP_OFF(ALT), D(false, false, KEY_ACT_COMMIT, 0), IDLE},
        // Flags changed.
        {"flags, armed, base released", 0, 0, 0, ARMED, FLAGS_EV(0), D(false, false, KEY_ACT_COMMIT, 0), IDLE},
        {"flags, armed, base released but shift held", 0, 0, 0, ARMED, FLAGS_EV(SHIFT), D(false, false, KEY_ACT_COMMIT, 0), IDLE},
        {"flags, armed, base still held", 0, 0, 0, ARMED, FLAGS_EV(ALT), PASS, ARMED},
        {"flags, armed, shift added", 0, 0, 0, ARMED, FLAGS_EV(ALT | SHIFT), PASS, ARMED},
        {"flags, not armed", 0, 0, 0, IDLE, FLAGS_EV(0), PASS, IDLE},
        // Stamps: before any early return, only for a non-repeat key.
        {"typing: not a call, quick reset", 0, 0, 0, IDLE, DOWN(kVK_ANSI_A, 0), PASS, ST(false, false, 0, 7, 0)},
        {"typing repeat: nothing stamped", 0, 0, 0, IDLE, REP(kVK_ANSI_A, 0), PASS, IDLE},
        {"shift+letter: not a call, quick reset", 0, 0, 0, IDLE, DOWN(kVK_ANSI_A, SHIFT), PASS, ST(false, false, 0, 7, 0)},
        {"Return: call, not quick", 0, 0, 0, IDLE, DOWN(kVK_Return, 0), PASS, ST(false, false, 0, NOW, 0)},
        {"Cmd+Tab passes and is a call and quick", 0, 0, 0, IDLE, DOWN(kVK_Tab, CMD), PASS, ST(false, false, 0, NOW, NOW)},
        {"Cmd+Tab repeat: nothing stamped", 0, 0, 0, IDLE, REP(kVK_Tab, CMD), PASS, IDLE},
        {"Cmd+W passes, a call but not quick", 0, 0, 0, IDLE, DOWN(kVK_ANSI_W, CMD), PASS, ST(false, false, 0, NOW, 0)},
        {"Cmd+C passes, call and quick", 0, 0, 0, IDLE, DOWN(kVK_ANSI_C, CMD), PASS, ST(false, false, 0, NOW, NOW)},
        {"flags event does not stamp", 0, 0, 0, IDLE, FLAGS_EV(CMD), PASS, IDLE},
        // Esc.
        {"Esc armed: swallow and cancel", 0, 0, 0, ARMED, DOWN(kVK_Escape, 0), D(true, true, KEY_ACT_NONE, 0), ST(false, false, 0, 7, 0)},
        {"Esc armed with base held", 0, 0, 0, ARMED, DOWN(kVK_Escape, ALT), D(true, true, KEY_ACT_NONE, 0), ST(false, false, 0, NOW, NOW)},
        {"Esc armed before the modifier test (Cmd)", 0, 0, 0, ARMED, DOWN(kVK_Escape, CMD), D(true, true, KEY_ACT_NONE, 0), ST(false, false, 0, NOW, NOW)},
        {"Esc armed repeat", 0, 0, 0, ARMED, REP(kVK_Escape, 0), D(true, true, KEY_ACT_NONE, 0), IDLE},
        {"Esc not armed passes", 0, 0, 0, IDLE, DOWN(kVK_Escape, 0), PASS, ST(false, false, 0, 7, 0)},
        {"Esc not armed with base passes", 0, 0, 0, IDLE, DOWN(kVK_Escape, ALT), PASS, ST(false, false, 0, NOW, NOW)},
        // Modifiers other than base.
        {"Cmd+Alt+1 passes", 0, 0, 0, IDLE, DOWN(kVK_ANSI_1, ALT | CMD), PASS, ST(false, false, 0, NOW, NOW)},
        {"Ctrl+Alt+Tab passes", 0, 0, 0, IDLE, DOWN(kVK_Tab, ALT | CTRL), PASS, ST(false, false, 0, NOW, NOW)},
        {"Ctrl+Alt+Tab armed: no change", 0, 0, 0, ARMED, DOWN(kVK_Tab, ALT | CTRL), PASS, ST(true, false, 0, NOW, NOW)},
        {"Cmd+Alt+[ passes", 0, 0, 0, IDLE, DOWN(kVK_ANSI_LeftBracket, ALT | CMD), PASS, ST(false, false, 0, NOW, NOW)},
        {"1 without base passes", 0, 0, 0, IDLE, DOWN(kVK_ANSI_1, 0), PASS, ST(false, false, 0, 7, 0)},
        {"Tab without base passes", 0, 0, 0, IDLE, DOWN(kVK_Tab, 0), PASS, ST(false, false, 0, 7, 0)},
        {"Shift+Tab without base passes", 0, 0, 0, IDLE, DOWN(kVK_Tab, SHIFT), PASS, ST(false, false, 0, 7, 0)},
        // Tab.
        {"Alt+Tab first press", 0, 0, 0, IDLE, DOWN(kVK_Tab, ALT), D(true, false, KEY_ACT_STEP, 1),
         ST(true, true, 1000, NOW, NOW)},
        {"Alt+Shift+Tab goes back", 0, 0, 0, IDLE, DOWN(kVK_Tab, ALT | SHIFT), D(true, false, KEY_ACT_STEP, -1),
         ST(true, true, 1000, NOW, NOW)},
        {"Alt+Tab first press while a step is pending", 0, 0, 0, ST(true, true, 999, 7, 8), DOWN(kVK_Tab, ALT),
         D(true, false, KEY_ACT_STEP, 1), ST(true, true, 1000, NOW, NOW)},
        {"Alt+Tab repeat within the interval: swallowed, still armed", 0, 0, 0, ST(false, false, 1000, 7, 8),
         REP_AT(kVK_Tab, ALT, 1100), D(true, false, KEY_ACT_NONE, 0), ST(true, false, 1000, 7, 8)},
        {"Alt+Tab repeat while pending", 0, 0, 0, ST(true, true, 1000, 7, 8), REP_AT(kVK_Tab, ALT, 5000),
         D(true, false, KEY_ACT_NONE, 0), ST(true, true, 1000, 7, 8)},
        {"Alt+Tab repeat after the interval", 0, 0, 0, ST(true, false, 1000, 7, 8), REP_AT(kVK_Tab, ALT, 1120),
         D(true, false, KEY_ACT_STEP, 1), ST(true, true, 1120, 7, 8)},
        {"Alt+Shift+Tab repeat after the interval", 0, 0, 0, ST(true, false, 1000, 7, 8), REP_AT(kVK_Tab, ALT | SHIFT, 1120),
         D(true, false, KEY_ACT_STEP, -1), ST(true, true, 1120, 7, 8)},
        {"Alt+Tab never cancels while armed", 0, 0, 0, ST(true, false, 0, 7, 8), DOWN(kVK_Tab, ALT),
         D(true, false, KEY_ACT_STEP, 1), ST(true, true, 1000, NOW, NOW)},
        {"cmd base: Cmd+Tab steps", CMD, 0, 0, IDLE, DOWN(kVK_Tab, CMD), D(true, false, KEY_ACT_STEP, 1),
         ST(true, true, 1000, NOW, NOW)},
        // Desktop keys.
        {"Alt+1 switches to 1", 0, 0, 0, IDLE, DOWN(kVK_ANSI_1, ALT), D(true, false, KEY_ACT_SWITCH, 1), ST(false, false, 0, NOW, NOW)},
        {"Alt+3 switches to 3", 0, 0, 0, IDLE, DOWN(kVK_ANSI_3, ALT), D(true, false, KEY_ACT_SWITCH, 3), ST(false, false, 0, NOW, NOW)},
        {"Alt+E switches to 6", 0, 0, 0, IDLE, DOWN(kVK_ANSI_E, ALT), D(true, false, KEY_ACT_SWITCH, 6), ST(false, false, 0, NOW, NOW)},
        {"Alt+Shift+2 moves to 2", 0, 0, 0, IDLE, DOWN(kVK_ANSI_2, ALT | SHIFT), D(true, false, KEY_ACT_MOVE, 2), ST(false, false, 0, NOW, NOW)},
        {"Alt+1 armed: cancel, then switch", 0, 0, 0, ARMED, DOWN(kVK_ANSI_1, ALT), D(true, true, KEY_ACT_SWITCH, 1), ST(false, false, 0, NOW, NOW)},
        {"Alt+Shift+2 armed: cancel, then move", 0, 0, 0, ARMED, DOWN(kVK_ANSI_2, ALT | SHIFT), D(true, true, KEY_ACT_MOVE, 2), ST(false, false, 0, NOW, NOW)},
        {"Alt+1 repeat: swallowed, nothing runs", 0, 0, 0, IDLE, REP(kVK_ANSI_1, ALT), D(true, false, KEY_ACT_NONE, 0), IDLE},
        {"Alt+1 repeat armed: no cancel, still armed", 0, 0, 0, ARMED, REP(kVK_ANSI_1, ALT), D(true, false, KEY_ACT_NONE, 0), ARMED},
        {"Alt+Shift+1 repeat armed: no cancel, still armed", 0, 0, 0, ARMED, REP(kVK_ANSI_1, ALT | SHIFT), D(true, false, KEY_ACT_NONE, 0), ARMED},
        {"a key past nkeys is not a desktop key", 0, DESK, 3, IDLE, DOWN(kVK_ANSI_Q, ALT), PASS, ST(false, false, 0, NOW, NOW)},
        {"the last key within nkeys is", 0, DESK, 3, IDLE, DOWN(kVK_ANSI_3, ALT), D(true, false, KEY_ACT_SWITCH, 3), ST(false, false, 0, NOW, NOW)},
        {"desktop keys come before the brackets: Alt+[", 0, WITH_BRACKET, 2, IDLE, DOWN(kVK_ANSI_LeftBracket, ALT),
         D(true, false, KEY_ACT_SWITCH, 2), ST(false, false, 0, NOW, NOW)},
        {"desktop keys come before the brackets: Alt+Shift+[", 0, WITH_BRACKET, 2, IDLE, DOWN(kVK_ANSI_LeftBracket, ALT | SHIFT),
         D(true, false, KEY_ACT_MOVE, 2), ST(false, false, 0, NOW, NOW)},
        // History.
        {"Alt+[ goes back", 0, 0, 0, IDLE, DOWN(kVK_ANSI_LeftBracket, ALT), D(true, false, KEY_ACT_HISTORY, -1), ST(false, false, 0, NOW, NOW)},
        {"Alt+] goes forward", 0, 0, 0, IDLE, DOWN(kVK_ANSI_RightBracket, ALT), D(true, false, KEY_ACT_HISTORY, 1), ST(false, false, 0, NOW, NOW)},
        {"Alt+[ armed: cancel, then history", 0, 0, 0, ARMED, DOWN(kVK_ANSI_LeftBracket, ALT), D(true, true, KEY_ACT_HISTORY, -1), ST(false, false, 0, NOW, NOW)},
        {"Alt+] armed: cancel, then history", 0, 0, 0, ARMED, DOWN(kVK_ANSI_RightBracket, ALT), D(true, true, KEY_ACT_HISTORY, 1), ST(false, false, 0, NOW, NOW)},
        {"Alt+[ repeat: swallowed, nothing runs", 0, 0, 0, IDLE, REP(kVK_ANSI_LeftBracket, ALT), D(true, false, KEY_ACT_NONE, 0), IDLE},
        {"Alt+] repeat armed: no cancel, still armed", 0, 0, 0, ARMED, REP(kVK_ANSI_RightBracket, ALT), D(true, false, KEY_ACT_NONE, 0), ARMED},
        {"Alt+Shift+[ passes", 0, 0, 0, IDLE, DOWN(kVK_ANSI_LeftBracket, ALT | SHIFT), PASS, ST(false, false, 0, NOW, NOW)},
        {"Alt+Shift+] passes", 0, 0, 0, IDLE, DOWN(kVK_ANSI_RightBracket, ALT | SHIFT), PASS, ST(false, false, 0, NOW, NOW)},
        {"Alt+Shift+[ armed passes, still armed", 0, 0, 0, ARMED, DOWN(kVK_ANSI_LeftBracket, ALT | SHIFT), PASS, ST(true, false, 0, NOW, NOW)},
        // Everything else.
        {"Alt+A passes", 0, 0, 0, IDLE, DOWN(kVK_ANSI_A, ALT), PASS, ST(false, false, 0, NOW, NOW)},
        {"Alt+Shift+A passes", 0, 0, 0, IDLE, DOWN(kVK_ANSI_A, ALT | SHIFT), PASS, ST(false, false, 0, NOW, NOW)},
        {"Alt+A armed passes, still armed", 0, 0, 0, ARMED, DOWN(kVK_ANSI_A, ALT), PASS, ST(true, false, 0, NOW, NOW)},
        {"Alt+Shift+A armed passes, still armed", 0, 0, 0, ARMED, DOWN(kVK_ANSI_A, ALT | SHIFT), PASS, ST(true, false, 0, NOW, NOW)},
        {"Alt+A repeat armed passes, still armed", 0, 0, 0, ARMED, REP(kVK_ANSI_A, ALT), PASS, ARMED},
    };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) decide(&rows[i]);
}

int main(void) {
    mods("alt", ALT, ALT, MODS_BASE);
    mods("alt+shift", ALT | SHIFT, ALT, MODS_BASE_SHIFT);
    mods("nothing", 0, ALT, MODS_OTHER);
    mods("shift alone", SHIFT, ALT, MODS_OTHER);
    mods("alt+cmd", ALT | CMD, ALT, MODS_OTHER);
    mods("alt+shift+ctrl", ALT | SHIFT | CTRL, ALT, MODS_OTHER);
    mods("cmd instead of alt", CMD, ALT, MODS_OTHER);
    mods("alt with caps lock and fn", ALT | kCGEventFlagMaskAlphaShift | kCGEventFlagMaskSecondaryFn, ALT, MODS_BASE);
    mods("cmd base: cmd", CMD, CMD, MODS_BASE);
    mods("cmd base: cmd+shift", CMD | SHIFT, CMD, MODS_BASE_SHIFT);
    mods("cmd base: alt", ALT, CMD, MODS_OTHER);
    mods("ctrl base: ctrl+alt", CTRL | ALT, CTRL, MODS_OTHER);

    allowed("first press", false, false, 1000, 0, true);
    allowed("first press while a step is pending", false, true, 1000, 999, true);
    allowed("repeat with a step pending", true, true, 1000, 0, false);
    allowed("repeat right after a step", true, false, 1030, 1000, false);
    allowed("repeat 119ms after a step", true, false, 1119, 1000, false);
    allowed("repeat 120ms after a step", true, false, 1120, 1000, true);
    allowed("repeat long after a step", true, false, 5000, 1000, true);

    call("Return", kVK_Return, 0, true);
    call("keypad Enter", kVK_ANSI_KeypadEnter, 0, true);
    call("Cmd+Tab", kVK_Tab, CMD, true);
    call("Cmd+C", kVK_ANSI_C, CMD, true);
    call("Ctrl+C", kVK_ANSI_C, CTRL, true);
    call("Alt+C", kVK_ANSI_C, ALT, true);
    call("Cmd+W is still a call", kVK_ANSI_W, CMD, true);
    call("a letter", kVK_ANSI_A, 0, false);
    call("Shift+letter is typing", kVK_ANSI_A, SHIFT, false);
    call("Tab alone", kVK_Tab, 0, false);
    call("letter with caps lock and fn", kVK_ANSI_A, kCGEventFlagMaskAlphaShift | kCGEventFlagMaskSecondaryFn, false);
    call("arrow key (fn and numeric pad flags)", kVK_UpArrow, kCGEventFlagMaskSecondaryFn | kCGEventFlagMaskNumericPad, false);

    quick("Cmd+Tab", kVK_Tab, CMD, true);
    quick("Cmd+C", kVK_ANSI_C, CMD, true);
    quick("Ctrl+C", kVK_ANSI_C, CTRL, true);
    quick("Alt+C", kVK_ANSI_C, ALT, true);
    quick("Cmd+Return", kVK_Return, CMD, true);
    quick("Alt+W is not a close", kVK_ANSI_W, ALT, true);
    quick("Ctrl+W is not a close", kVK_ANSI_W, CTRL, true);
    quick("Cmd+W closes", kVK_ANSI_W, CMD, false);
    quick("Cmd+Q quits", kVK_ANSI_Q, CMD, false);
    quick("Cmd+M minimizes", kVK_ANSI_M, CMD, false);
    quick("Cmd+H hides", kVK_ANSI_H, CMD, false);
    quick("Return", kVK_Return, 0, false);
    quick("keypad Enter", kVK_ANSI_KeypadEnter, 0, false);
    quick("a letter", kVK_ANSI_A, 0, false);
    quick("Shift+letter is typing", kVK_ANSI_A, SHIFT, false);
    quick("letter with caps lock and fn", kVK_ANSI_A, kCGEventFlagMaskAlphaShift | kCGEventFlagMaskSecondaryFn, false);

    decide_all();

    puts(failed ? "FAILED" : "ok");
    return failed != 0;
}
