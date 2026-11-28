#include "keyrules.h"

// macOS virtual key codes (kVK_ in Carbon's Events.h).
#define KEY_RETURN 0x24
#define KEY_ENTER 0x4C
#define KEY_W 0x0D
#define KEY_Q 0x0C
#define KEY_M 0x2E
#define KEY_H 0x04
#define KEY_ESC 0x35
#define KEY_LBRACKET 0x21
#define KEY_RBRACKET 0x1E

mods_t keyrules_mods(CGEventFlags flags, CGEventFlags base) {
    CGEventFlags mods = flags & (kCGEventFlagMaskAlternate | kCGEventFlagMaskShift |
                                 kCGEventFlagMaskCommand | kCGEventFlagMaskControl);
    if (mods == base) return MODS_BASE;
    if (mods == (base | kCGEventFlagMaskShift)) return MODS_BASE_SHIFT;
    return MODS_OTHER;
}

bool keyrules_step_allowed(bool repeat, bool step_pending, uint64_t now_ns, uint64_t last_step_ns) {
    return !(repeat && (step_pending || now_ns - last_step_ns < KEYRULES_REPEAT_MS * 1000000ull));
}

static bool is_shortcut(CGEventFlags flags) {
    return flags & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate);
}

bool keyrules_is_call_key(int64_t key, CGEventFlags flags) {
    return key == KEY_RETURN || key == KEY_ENTER || is_shortcut(flags);
}

bool keyrules_is_quick_key(int64_t key, CGEventFlags flags) {
    bool leave = (flags & kCGEventFlagMaskCommand) && (key == KEY_W || key == KEY_Q || key == KEY_M || key == KEY_H);
    return is_shortcut(flags) && !leave;
}

key_decision_t keyrules_decide(key_state_t *s, const key_event_t *e, CGEventFlags base, const int64_t *keys, int nkeys) {
    key_decision_t d = {0};
    if (e->type != KEYEV_DOWN) {
        if (s->armed && !(e->flags & base)) {
            s->armed = false;
            d.action = KEY_ACT_COMMIT;
        }
        return d;
    }
    int64_t key = e->key;
    // Before any early return, so Cmd-Tab and other apps' shortcuts are seen. Held-down repeats are not calls.
    if (!e->repeat) {
        if (keyrules_is_call_key(key, e->flags)) s->last_call_key = e->now_abs;
        // Every key replaces the last, so a ⌘W after a shortcut cancels it.
        s->last_quick_key = keyrules_is_quick_key(key, e->flags) ? e->now_abs : 0;
    }
    if (s->armed && key == KEY_ESC) {
        s->armed = false;
        d.swallow = d.cancel = true;
        return d;
    }

    mods_t mods = keyrules_mods(e->flags, base);
    if (mods == MODS_OTHER) return d;
    bool alt = mods == MODS_BASE;

    if (key == KEYRULES_KEY_TAB) {
        s->armed = true;
        d.swallow = true;
        if (!keyrules_step_allowed(e->repeat, s->step_pending, e->now_ns, s->last_step_ns)) return d;
        s->last_step_ns = e->now_ns;
        s->step_pending = true;
        d.action = KEY_ACT_STEP;
        d.arg = alt ? 1 : -1;
        return d;
    }
    for (int i = 0; i < nkeys; i++) {
        if (key != keys[i]) continue;
        d.swallow = true;
        if (e->repeat) return d;
        d.cancel = s->armed;
        s->armed = false;
        d.action = alt ? KEY_ACT_SWITCH : KEY_ACT_MOVE;
        d.arg = i + 1;
        return d;
    }
    if (alt && (key == KEY_LBRACKET || key == KEY_RBRACKET)) {
        d.swallow = true;
        if (e->repeat) return d;
        d.cancel = s->armed;
        s->armed = false;
        d.action = KEY_ACT_HISTORY;
        d.arg = key == KEY_LBRACKET ? -1 : 1;
    }
    return d;
}
