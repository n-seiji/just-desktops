#include "hotkey.h"
#include "config.h"
#include "keyrules.h"
#include "switcher.h"
#include "wm.h"
#include <dispatch/dispatch.h>

// Hotkeys, with ⌥ standing for the base modifier (`modifier` in config: opt, cmd or ctrl; default opt):
//   ⌥<key>   switch to the desktop bound to the key (`keys` in config, default 1 2 3 q w e)
//   ⌥⇧<key>  move focused window to that desktop
//   ⌥[ / ⌥]  back / forward through the last HIST_MAX focused windows, across desktops
//   ⌥Tab / ⌥⇧Tab  open the window switcher / move the selection; releasing ⌥ commits, Esc cancels
//                 (`switcher = off`: no panel, each Tab focuses the next window at once). ⌥<key> while it is open closes it first.
// Only the base modifier (plus Shift) may be down; any other extra modifier lets the key through.

static CFMachPortRef tap;
static CGEventFlags base; // the configured base modifier
static int64_t keys[JD_MAX_WS]; // the desktop keys, config_key(i) for each desktop
static int nkeys;
static key_state_t st;

double hotkey_last_call_key(void) { return st.last_call_key; }
double hotkey_last_quick_key(void) { return st.last_quick_key; }

static void apply(key_decision_t d) {
    if (d.cancel) dispatch_async(dispatch_get_main_queue(), ^{ switcher_cancel(); });
    int arg = d.arg;
    switch (d.action) {
    case KEY_ACT_NONE:
        break;
    case KEY_ACT_COMMIT:
        dispatch_async(dispatch_get_main_queue(), ^{ switcher_commit(); });
        break;
    case KEY_ACT_STEP:
        dispatch_async(dispatch_get_main_queue(), ^{
            switcher_step(arg);
            st.step_pending = false;
        });
        break;
    case KEY_ACT_SWITCH:
        dispatch_async(dispatch_get_main_queue(), ^{
            if (arg == wm_current()) {
                wm_dismiss_every();
            } else {
                wm_set_trigger("hotkey");
                wm_switch(arg);
            }
        });
        break;
    case KEY_ACT_MOVE:
        dispatch_async(dispatch_get_main_queue(), ^{ wm_move_focused(arg); });
        break;
    case KEY_ACT_HISTORY:
        dispatch_async(dispatch_get_main_queue(), ^{ wm_history(arg); });
        break;
    }
}

static CGEventRef on_key(CGEventTapProxy proxy, CGEventType type, CGEventRef ev, void *ctx) {
    (void)proxy; (void)ctx;
    key_event_t e = {0};
    switch (type) {
    case kCGEventTapDisabledByTimeout:
    case kCGEventTapDisabledByUserInput:
        CGEventTapEnable(tap, true);
        e.type = KEYEV_TAP_OFF;
        e.flags = CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
        break;
    case kCGEventFlagsChanged:
        e.type = KEYEV_FLAGS;
        e.flags = CGEventGetFlags(ev);
        break;
    case kCGEventKeyDown:
        e.type = KEYEV_DOWN;
        e.key = CGEventGetIntegerValueField(ev, kCGKeyboardEventKeycode);
        e.repeat = CGEventGetIntegerValueField(ev, kCGKeyboardEventAutorepeat) != 0;
        e.flags = CGEventGetFlags(ev);
        // The tap sees every key typed: read each clock only for the rule that uses it.
        if (!e.repeat) e.now_abs = CFAbsoluteTimeGetCurrent();
        if (e.key == KEYRULES_KEY_TAB) e.now_ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        break;
    default:
        return ev;
    }
    key_decision_t d = keyrules_decide(&st, &e, base, keys, nkeys);
    apply(d);
    return d.swallow ? NULL : ev;
}

bool hotkey_init(void) {
    base = config_modifier();
    nkeys = config_workspaces();
    for (int i = 0; i < nkeys; i++) keys[i] = config_key(i);
    tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
                           CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventFlagsChanged), on_key, NULL);
    if (!tap) return false;
    CFRunLoopSourceRef src = CFMachPortCreateRunLoopSource(NULL, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), src, kCFRunLoopDefaultMode);
    CFRelease(src);
    CGEventTapEnable(tap, true);
    return true;
}
