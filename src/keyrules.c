#include "keyrules.h"

// macOS virtual key codes (kVK_ in Carbon's Events.h).
#define KEY_RETURN 0x24
#define KEY_ENTER 0x4C
#define KEY_W 0x0D
#define KEY_Q 0x0C
#define KEY_M 0x2E
#define KEY_H 0x04

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
