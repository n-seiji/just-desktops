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

// The whole decision of the tap for one event: what it swallows, and what runs because of it. The caller reads the
// event and the clocks, applies the decision (cancel first, then the action) and holds the state between calls.
typedef enum { KEYEV_DOWN, KEYEV_FLAGS, KEYEV_TAP_OFF } key_event_type_t;

// The Tab key. The caller reads the throttle clock (now_ns) only for it.
#define KEYRULES_KEY_TAB 0x30

typedef struct {
    key_event_type_t type;
    int64_t key;       // KEYEV_DOWN only
    bool repeat;       // KEYEV_DOWN only
    CGEventFlags flags; // the event's; for KEYEV_TAP_OFF the session's, since the release may have been missed while the tap was off
    double now_abs;    // CFAbsoluteTime for the call and quick-key stamps; read only when KEYEV_DOWN and !repeat
    uint64_t now_ns;   // CLOCK_UPTIME_RAW for the Tab throttle; read only when the key is KEYRULES_KEY_TAB
} key_event_t;

typedef struct {
    bool armed;        // the switcher is open (or about to be): the base modifier is held since a Tab
    bool step_pending; // see keyrules_step_allowed
    uint64_t last_step_ns;
    double last_call_key;  // see keyrules_is_call_key
    double last_quick_key; // see keyrules_is_quick_key; 0 when the last key press was not one
} key_state_t;

typedef enum { KEY_ACT_NONE, KEY_ACT_COMMIT, KEY_ACT_STEP, KEY_ACT_SWITCH, KEY_ACT_MOVE, KEY_ACT_HISTORY } key_action_t;

typedef struct {
    bool swallow;
    bool cancel;       // close the switcher; runs before the action
    key_action_t action;
    int arg;           // direction (+1, -1) for STEP and HISTORY, desktop number (1-based) for SWITCH and MOVE
} key_decision_t;

// Decides one event and updates *s. keys[0..nkeys) are the desktop keys, the first one is desktop 1; they are
// tried before the bracket keys.
key_decision_t keyrules_decide(key_state_t *s, const key_event_t *e, CGEventFlags base, const int64_t *keys, int nkeys);
