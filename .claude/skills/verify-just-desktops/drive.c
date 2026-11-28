// Drives the desktop the way a user does, for verify-just-desktops. Needs Accessibility for the terminal.
//   drive key <keycode>   Option + key (18 is "1")
//   drive click <x> <y>   left click at a point, in global display points
//   drive mc              prints 1 while Mission Control is up, else 0
//   drive windows [all]   on-screen windows above layer 0 (all: layer 0 too, so parked ones show): owner|layer|WxH+X+Y|name
//   drive chord <opt|optshift> <keycode> [-n steps] [-t hold_ms] [-a]
//                         presses the modifier as a flags-changed event, presses the key n times with the
//                         modifier held, waits hold_ms, releases the modifier. -a sends the key as one held
//                         key (the first press, then autorepeats). Interrupted, it still releases.
//   drive probe           posts a fixed table of key events and prints "<name> pass|swallow" per row
#include <ApplicationServices/ApplicationServices.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define KEY_OPT 58
#define KEY_SHIFT 56
#define KEY_TAB 0x30
#define KEY_ESC 0x35
#define KEY_LBRACKET 0x21
#define KEY_RBRACKET 0x1E
#define KEY_ONE 18
#define KEY_A 0

static volatile sig_atomic_t stop;

static void on_signal(int sig) {
    (void)sig;
    stop = 1;
}

static void key(int code) {
    for (int down = 1; down >= 0; down--) {
        CGEventRef e = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)code, down);
        CGEventSetFlags(e, kCGEventFlagMaskAlternate);
        CGEventPost(kCGHIDEventTap, e);
        CFRelease(e);
        usleep(30000);
    }
}

static void click(CGPoint p) {
    CGEventType steps[] = {kCGEventMouseMoved, kCGEventLeftMouseDown, kCGEventLeftMouseUp};
    useconds_t pause[] = {120000, 40000, 0};
    for (int i = 0; i < 3; i++) {
        CGEventRef e = CGEventCreateMouseEvent(NULL, steps[i], p, kCGMouseButtonLeft);
        CGEventPost(kCGHIDEventTap, e);
        CFRelease(e);
        usleep(pause[i]);
    }
}

static int windows(bool only_mc, bool all) {
    CFArrayRef a = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
    int mc = 0;
    for (CFIndex i = 0; a && i < CFArrayGetCount(a); i++) {
        CFDictionaryRef d = CFArrayGetValueAtIndex(a, i);
        char owner[128] = "", name[128] = "";
        CFStringRef o = CFDictionaryGetValue(d, kCGWindowOwnerName), n = CFDictionaryGetValue(d, kCGWindowName);
        if (o) CFStringGetCString(o, owner, sizeof owner, kCFStringEncodingUTF8);
        if (n) CFStringGetCString(n, name, sizeof name, kCFStringEncodingUTF8);
        int layer = 0;
        CGRect r = CGRectZero;
        CFNumberGetValue(CFDictionaryGetValue(d, kCGWindowLayer), kCFNumberIntType, &layer);
        CGRectMakeWithDictionaryRepresentation(CFDictionaryGetValue(d, kCGWindowBounds), &r);
        // The same test as wm_mission_control_up in src/wm_list.c: keep the two alike.
        CGSize full = CGDisplayBounds(CGMainDisplayID()).size;
        if (layer == 19 && !strcmp(owner, "WindowManager") && r.size.width >= full.width && r.size.height >= full.height)
            mc = 1;
        if (!only_mc && (all || layer != 0))
            printf("%s|%d|%gx%g+%g+%g|%s\n", owner, layer, r.size.width, r.size.height, r.origin.x, r.origin.y, name);
    }
    if (a) CFRelease(a);
    if (only_mc) printf("%d\n", mc);
    return 0;
}

static void catch_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

// Sleeps in slices and runs the loop, so a tap callback is delivered. Returns early once a signal arrived.
static void nap(int ms) {
    for (; ms > 0 && !stop; ms -= 10) {
        usleep(10000);
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
    }
}

static void post_flags(CGKeyCode code, CGEventFlags flags) {
    CGEventRef e = CGEventCreateKeyboardEvent(NULL, code, flags != 0);
    CGEventSetType(e, kCGEventFlagsChanged);
    CGEventSetFlags(e, flags);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

// tag 0 leaves the event unmarked.
static void post_key(CGKeyCode code, bool down, CGEventFlags flags, bool repeat, int64_t tag) {
    CGEventRef e = CGEventCreateKeyboardEvent(NULL, code, down);
    CGEventSetFlags(e, flags);
    if (repeat) CGEventSetIntegerValueField(e, kCGKeyboardEventAutorepeat, 1);
    if (tag) CGEventSetIntegerValueField(e, kCGEventSourceUserData, tag);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

static void mod_down(CGEventFlags mods) {
    post_flags(KEY_OPT, kCGEventFlagMaskAlternate);
    if (mods & kCGEventFlagMaskShift) {
        nap(10);
        post_flags(KEY_SHIFT, mods);
    }
}

// The final event has flags 0: the tap commits a switcher when it sees the base modifier gone.
static void mod_up(CGEventFlags mods) {
    if (mods & kCGEventFlagMaskShift) {
        post_flags(KEY_SHIFT, kCGEventFlagMaskAlternate);
        nap(10);
    }
    post_flags(KEY_OPT, 0);
}

static int chord(CGEventFlags mods, CGKeyCode code, int steps, int hold_ms, bool autorepeat) {
    mod_down(mods);
    nap(30);
    for (int i = 0; i < steps && !stop; i++) {
        if (i) nap(autorepeat ? 30 : 60);
        post_key(code, true, mods, autorepeat && i > 0, 0);
        if (!autorepeat) {
            nap(30);
            post_key(code, false, mods, false, 0);
        }
    }
    if (autorepeat) post_key(code, false, mods, false, 0);
    nap(hold_ms);
    mod_up(mods);
    nap(30);
    return stop ? 130 : 0;
}

static bool parse_num(const char *s, long lo, long hi, long *out) {
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || *end || v < lo || v > hi) return false;
    *out = v;
    return true;
}

static int chord_main(int argc, char **argv) {
    CGEventFlags mods;
    long code, steps = 1, hold = 0;
    bool autorepeat = false;
    if (argc < 4 || !parse_num(argv[3], 0, 127, &code)) return -1;
    if (!strcmp(argv[2], "opt")) mods = kCGEventFlagMaskAlternate;
    else if (!strcmp(argv[2], "optshift")) mods = kCGEventFlagMaskAlternate | kCGEventFlagMaskShift;
    else return -1;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "-a")) autorepeat = true;
        else if (!strcmp(argv[i], "-n") && i + 1 < argc && parse_num(argv[++i], 1, 100, &steps)) continue;
        else if (!strcmp(argv[i], "-t") && i + 1 < argc && parse_num(argv[++i], 0, 60000, &hold)) continue;
        else return -1;
    }
    catch_signals();
    return chord(mods, (CGKeyCode)code, (int)steps, (int)hold, autorepeat);
}

// Probe. The app's tap is a session tap at the head (src/hotkey.c). Events posted at the HID tap pass it first,
// then reach this tap at the annotated session point, after every session tap. A key-down that arrives here was
// let through, one that never does was swallowed. The tap is an active one so that it can drop the probe's own
// events: a key let through would otherwise reach the frontmost app.
#define PROBE_TAG 0x4A445000ll // "JDP", the row number is added in the low byte
#define OPT kCGEventFlagMaskAlternate
#define SHIFT kCGEventFlagMaskShift

typedef struct {
    const char *name;
    CGEventFlags flags;
    CGKeyCode key;
    bool repeat;
    bool held; // the base modifier is physically down for this row (a flags-changed event), as the switcher needs
} row_t;

// Assumes the default `modifier` (opt) and `keys` (1 2 3 q w e). The held rows leave the switcher armed, then Esc cancels it.
static const row_t rows[] = {
    {"cmd-opt-1", kCGEventFlagMaskCommand | OPT, KEY_ONE, false, false},
    {"ctrl-opt-tab", kCGEventFlagMaskControl | OPT, KEY_TAB, false, false},
    {"opt-shift-lbracket", OPT | SHIFT, KEY_LBRACKET, false, false},
    {"esc-not-armed", 0, KEY_ESC, false, false},
    {"opt-1-autorepeat", OPT, KEY_ONE, true, false},
    {"opt-shift-1-autorepeat", OPT | SHIFT, KEY_ONE, true, false},
    {"opt-a", OPT, KEY_A, false, false},
    {"opt-tab", OPT, KEY_TAB, false, true},
    {"opt-shift-tab", OPT | SHIFT, KEY_TAB, false, true},
    {"esc-armed", OPT, KEY_ESC, false, true},
    // Releasing the modifier commits, so this Esc must find no switcher armed.
    {"opt-tab-again", OPT, KEY_TAB, false, true},
    {"esc-after-release", 0, KEY_ESC, false, false},
    {"opt-lbracket", OPT, KEY_LBRACKET, false, false},
    {"opt-rbracket", OPT, KEY_RBRACKET, false, false},
};
#define ROWS ((int)(sizeof rows / sizeof rows[0]))

static CFMachPortRef probe_tap;
static bool seen[ROWS];

static CGEventRef on_probe(CGEventTapProxy proxy, CGEventType type, CGEventRef ev, void *ctx) {
    (void)proxy; (void)ctx;
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        CGEventTapEnable(probe_tap, true);
        return ev;
    }
    int64_t tag = CGEventGetIntegerValueField(ev, kCGEventSourceUserData);
    if ((tag & ~0xFFll) != PROBE_TAG) return ev;
    int row = (int)(tag & 0xFF);
    if (type == kCGEventKeyDown && row < ROWS) seen[row] = true;
    return NULL;
}

static bool wait_seen(int row, int ms) {
    for (; ms > 0 && !stop && !seen[row]; ms -= 10) nap(10);
    return seen[row];
}

static int probe(void) {
    probe_tap = CGEventTapCreate(kCGAnnotatedSessionEventTap, kCGTailAppendEventTap, kCGEventTapOptionDefault,
                                 CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp), on_probe, NULL);
    if (!probe_tap) {
        fprintf(stderr, "drive probe: cannot create the event tap. Give this terminal Accessibility and Input Monitoring.\n");
        return 1;
    }
    CFRunLoopSourceRef src = CFMachPortCreateRunLoopSource(NULL, probe_tap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), src, kCFRunLoopDefaultMode);
    CFRelease(src);
    CGEventTapEnable(probe_tap, true);
    catch_signals();

    bool down = false;
    bool passed[ROWS] = {false};
    int done = 0;
    for (; done < ROWS && !stop; done++) {
        const row_t *r = &rows[done];
        if (r->held != down) {
            if (r->held) mod_down(OPT);
            else mod_up(OPT);
            nap(30);
        }
        down = r->held;
        int64_t tag = PROBE_TAG + done;
        post_key(r->key, true, r->flags, r->repeat, tag);
        passed[done] = wait_seen(done, 150);
        post_key(r->key, false, r->flags, false, tag);
        nap(30);
    }
    if (down) mod_up(OPT);
    nap(100);
    CFRelease(probe_tap);
    if (stop) return 130;
    for (int i = 0; i < ROWS; i++) printf("%s %s\n", rows[i].name, passed[i] ? "pass" : "swallow");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "key")) return key(atoi(argv[2])), 0;
    if (argc == 4 && !strcmp(argv[1], "click")) return click(CGPointMake(atof(argv[2]), atof(argv[3]))), 0;
    if (argc == 2 && !strcmp(argv[1], "mc")) return windows(true, false);
    if (argc == 2 && !strcmp(argv[1], "windows")) return windows(false, false);
    if (argc == 3 && !strcmp(argv[1], "windows") && !strcmp(argv[2], "all")) return windows(false, true);
    if (argc == 2 && !strcmp(argv[1], "probe")) return probe();
    if (argc >= 2 && !strcmp(argv[1], "chord")) {
        int rc = chord_main(argc, argv);
        if (rc >= 0) return rc;
    }
    fprintf(stderr, "usage: drive key <keycode> | click <x> <y> | mc | windows [all] | probe\n"
                    "       drive chord <opt|optshift> <keycode> [-n steps] [-t hold_ms] [-a]\n");
    return 2;
}
