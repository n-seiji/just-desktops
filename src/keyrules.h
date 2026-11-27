#pragma once
// The decisions of the keyboard tap that depend only on the key, the modifier flags and timing.
#include <ApplicationServices/ApplicationServices.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum { MODS_OTHER, MODS_BASE, MODS_BASE_SHIFT } mods_t;

// Exact-set match of the four modifiers: only base (MODS_BASE), or base with Shift (MODS_BASE_SHIFT),
// is down. Anything else, an extra Command or Control for instance, is MODS_OTHER and the key goes
// through. Caps Lock, Fn and the other flags are ignored.
mods_t keyrules_mods(CGEventFlags flags, CGEventFlags base);

// A held Tab repeats at the OS rate (~30ms), faster than a step is worth and than the focus calls of
// `switcher = off` complete, so the selection would race around and keep moving after the release.
// A repeat steps only when the last step is done and KEYRULES_REPEAT_MS old. A first press always steps.
#define KEYRULES_REPEAT_MS 120
bool keyrules_step_allowed(bool repeat, bool step_pending, uint64_t now_ns, uint64_t last_step_ns);

// Whether a key press (the caller skips repeats) is you calling a window: Return, keypad Enter, or a shortcut,
// which is any key with Command, Control or Option down. Typing, with no modifier or only Shift, is not.
// Caps Lock, Fn and the other flags are ignored.
bool keyrules_is_call_key(int64_t key, CGEventFlags flags);

// Whether a key press (the caller skips repeats) is a shortcut that switches desktops at once, without the 0.3 s
// wait: any shortcut but Command with W, Q, M or H. Close, quit, minimize and hide move focus on their own, so
// the focus after them must wait. Return or Enter with none of those three down is a call but not quick.
bool keyrules_is_quick_key(int64_t key, CGEventFlags flags);
