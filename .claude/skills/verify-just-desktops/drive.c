// Drives the desktop the way a user does, for verify-just-desktops. Needs Accessibility for the terminal.
//   drive key <keycode>   Option + key (18 is "1")
//   drive click <x> <y>   left click at a point, in global display points
//   drive mc              prints 1 while Mission Control is up, else 0
//   drive windows         on-screen windows above layer 0: owner|layer|WxH+X+Y|name
#include <ApplicationServices/ApplicationServices.h>
#include <string.h>
#include <unistd.h>

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

static int windows(bool only_mc) {
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
        if (!only_mc && layer != 0)
            printf("%s|%d|%gx%g+%g+%g|%s\n", owner, layer, r.size.width, r.size.height, r.origin.x, r.origin.y, name);
    }
    if (a) CFRelease(a);
    if (only_mc) printf("%d\n", mc);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "key")) return key(atoi(argv[2])), 0;
    if (argc == 4 && !strcmp(argv[1], "click")) return click(CGPointMake(atof(argv[2]), atof(argv[3]))), 0;
    if (argc == 2 && !strcmp(argv[1], "mc")) return windows(true);
    if (argc == 2 && !strcmp(argv[1], "windows")) return windows(false);
    fprintf(stderr, "usage: drive key <keycode> | click <x> <y> | mc | windows\n");
    return 2;
}
