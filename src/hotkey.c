#include "hotkey.h"
#include "config.h"
#include "keyrules.h"
#include "switcher.h"
#include "wm.h"
#include <dispatch/dispatch.h>

// Hotkeys, with ⌥ standing for the base modifier (`modifier` in config: opt, cmd or ctrl; default opt):
//   ⌥<key>   switch to the desktop bound to the key (`keys` in config, default 1 2 3 q w e)
//   ⌥⇧<key>  move focused window to that desktop
//   ⌥[ / ⌥]  back / forward through the last 10 focused windows, across desktops
//   ⌥Tab / ⌥⇧Tab  open the window switcher / move the selection; releasing ⌥ commits, Esc cancels
//                 (`switcher = off`: no panel, each Tab focuses the next window at once). ⌥<key> while it is open closes it first.
// Only the base modifier (plus Shift) may be down; any other extra modifier lets the key through.

#define KEY_TAB 0x30
#define KEY_ESC 0x35
#define KEY_RETURN 0x24
#define KEY_ENTER 0x4C
#define KEY_LBRACKET 0x21
#define KEY_RBRACKET 0x1E

static CFMachPortRef tap;
static CGEventFlags base; // the configured base modifier
static bool armed; // switcher open (or about to be): ⌥ is held since a Tab
static bool step_pending; // see keyrules_step_allowed: the rest of a held Tab's repeats are swallowed
static uint64_t last_step_ns;

static void commit(void) {
    armed = false;
    dispatch_async(dispatch_get_main_queue(), ^{ switcher_commit(); });
}

static void cancel(void) {
    armed = false;
    dispatch_async(dispatch_get_main_queue(), ^{ switcher_cancel(); });
}

static double last_call_key;

double hotkey_last_call_key(void) { return last_call_key; }

static CGEventRef on_key(CGEventTapProxy proxy, CGEventType type, CGEventRef ev, void *ctx) {
    (void)proxy; (void)ctx;
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        CGEventTapEnable(tap, true);
        // The ⌥ release may have been missed while the tap was off.
        if (armed && !(CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & base)) commit();
        return ev;
    }
    if (type == kCGEventFlagsChanged) {
        if (armed && !(CGEventGetFlags(ev) & base)) commit();
        return ev;
    }
    if (type != kCGEventKeyDown) return ev;
    // Before any early return, so Cmd-Tab and other apps' shortcuts are seen. Typing (no modifier, or only Shift)
    // and held-down repeats are not calls.
    if (CGEventGetIntegerValueField(ev, kCGKeyboardEventAutorepeat) == 0) {
        int64_t code = CGEventGetIntegerValueField(ev, kCGKeyboardEventKeycode);
        bool shortcut = CGEventGetFlags(ev) & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate);
        if (code == KEY_RETURN || code == KEY_ENTER || shortcut) last_call_key = CFAbsoluteTimeGetCurrent();
    }
    if (armed && CGEventGetIntegerValueField(ev, kCGKeyboardEventKeycode) == KEY_ESC) {
        cancel();
        return NULL;
    }

    mods_t mods = keyrules_mods(CGEventGetFlags(ev), base);
    if (mods == MODS_OTHER) return ev;
    bool alt = mods == MODS_BASE;

    int64_t key = CGEventGetIntegerValueField(ev, kCGKeyboardEventKeycode);
    bool repeat = CGEventGetIntegerValueField(ev, kCGKeyboardEventAutorepeat) != 0;

    if (key == KEY_TAB) {
        int dir = alt ? 1 : -1;
        armed = true;
        uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        if (!keyrules_step_allowed(repeat, step_pending, now, last_step_ns)) return NULL;
        last_step_ns = now;
        step_pending = true;
        dispatch_async(dispatch_get_main_queue(), ^{
            switcher_step(dir);
            step_pending = false;
        });
        return NULL;
    }
    for (int i = 0; i < config_workspaces(); i++) {
        if (key != config_key(i)) continue;
        int ws = i + 1;
        if (!repeat) {
            if (armed) cancel();
            if (alt) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (ws == wm_current()) {
                        wm_dismiss_every();
                    } else {
                        wm_set_trigger("hotkey");
                        wm_switch(ws);
                    }
                });
            } else {
                dispatch_async(dispatch_get_main_queue(), ^{ wm_move_focused(ws); });
            }
        }
        return NULL;
    }
    if (alt && (key == KEY_LBRACKET || key == KEY_RBRACKET)) {
        if (!repeat) {
            if (armed) cancel();
            int dir = key == KEY_LBRACKET ? -1 : 1;
            dispatch_async(dispatch_get_main_queue(), ^{ wm_history(dir); });
        }
        return NULL;
    }
    return ev;
}

bool hotkey_init(void) {
    base = config_modifier();
    tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
                           CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventFlagsChanged), on_key, NULL);
    if (!tap) return false;
    CFRunLoopSourceRef src = CFMachPortCreateRunLoopSource(NULL, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), src, kCFRunLoopDefaultMode);
    CFRelease(src);
    CGEventTapEnable(tap, true);
    return true;
}
