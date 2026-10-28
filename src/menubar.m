#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#include "menubar.h"
#include "menubar_priv.h"
#include "strip.h"
#include "wm.h"
#include <string.h>

// Optional menu bar item: one line-art glyph per window on the current desktop (an app's
// windows side by side), then the desktop's key in an outlined box at the right end. The
// item keeps room for STRIP_MIN_SLOTS slots and
// the strip is right-aligned in it, so the key stays where it is and switching desktops changes
// no width (macOS animates the menu bar on every width change). It widens at once for more and
// narrows STRIP_SHRINK_S seconds after the extra room was last needed. The focused window's
// glyph is cut out of a filled tile. Past JD_MENUBAR_SLOTS windows the last slot reads "+k"; a click
// on it opens a panel (menubar_overflow.m) listing the windows that have no glyph.
// The image is a template, so macOS tints it for light and dark menu bars.

enum {
    SCALE = 2,       // pixels per point
    HEIGHT = 22,     // points, the status item's thickness
    PAD = 1,         // tile edge to glyph
};

static NSStatusItem *item;
static menubar_mode_t mode;

static int last_ws;
static int last_n = -1;
static int last_over;
static uint32_t last_hash;
static pid_t last_pids[JD_MENUBAR_SLOTS];
static uint32_t last_wids[JD_MENUBAR_SLOTS];
static uint32_t last_front;
static int reserve;         // slots the item is wide for
static int last_need = -1;  // slots the strip needs
static unsigned shrink_gen; // bumped to cancel a queued shrink

@interface JDClickTarget : NSObject
- (void)clicked:(id)sender;
@end

@implementation JDClickTarget
- (void)clicked:(id)sender {
    NSButton *b = sender;
    NSEvent *e = NSApp.currentEvent;
    if (!e) return;
    if (mode == MB_ICON || e.type == NSEventTypeRightMouseUp || e.modifierFlags & NSEventModifierFlagControl) {
        // The status item places its own menu under itself; detached again at once so left clicks reach clicked: next time.
        item.menu = menubar_menu(mode);
        [b performClick:nil];
        item.menu = nil;
        return;
    }
    if (last_n <= 0) return;
    if (menubar_overflow_is_open()) {
        menubar_overflow_close();
        return;
    }
    // The pointer, not the event: macOS may hand the item a click located at the button's centre.
    NSPoint at = [b convertPoint:[b.window convertPointFromScreen:NSEvent.mouseLocation] fromView:nil];
    strip_hit_t hit = strip_hit(at.x, b.bounds.size.width, reserve, last_n, last_over);
    if (hit.slot >= 0) {
        wm_set_trigger("menubar");
        wm_focus_wid(last_wids[hit.slot]);
        return;
    }
    if (menubar_overflow_just_closed()) return;
    if (hit.overflow) {
        CGFloat left = strip_overflow_left(b.bounds.size.width, reserve, last_n, last_over);
        menubar_overflow_open(b, NSMakeRect(left, 0, STRIP_SLOT, b.bounds.size.height));
    }
}
@end

static JDClickTarget *target;

// The app glyph of icon mode, from the logo mark: a rounded outline with a filled rounded square inside.
static NSImage *icon_image(void) {
    NSImage *img = [NSImage imageWithSize:NSMakeSize(18, 18) flipped:NO drawingHandler:^BOOL(NSRect r) {
        (void)r;
        [NSColor.blackColor set];
        NSBezierPath *outer = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(2.25, 2.25, 13.5, 13.5) xRadius:3.5 yRadius:3.5];
        outer.lineWidth = 1.5;
        [outer stroke];
        [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(5.5, 5.5, 7, 7) xRadius:1.8 yRadius:1.8] fill];
        return YES;
    }];
    img.template = YES;
    return img;
}

// text centred by its real outline bounds, not the line box, in the slot at x and snapped to the
// pixel grid so it stays crisp at 1x and 2x.
static void draw_centered(CGContextRef c, CGFloat x, CFStringRef text, CGFloat size) {
    CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontEmphasizedSystem, size, NULL);
    CFTypeRef keys[] = {kCTFontAttributeName, kCTForegroundColorFromContextAttributeName};
    CFTypeRef vals[] = {font, kCFBooleanTrue};
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, (const void **)keys, (const void **)vals, 2,
                                               &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef str = CFAttributedStringCreate(NULL, text, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(str);
    CGRect b = CTLineGetBoundsWithOptions(line, kCTLineBoundsUseGlyphPathBounds);
    CGFloat px = x + STRIP_SLOT / 2.0 - CGRectGetMidX(b), py = HEIGHT / 2.0 - CGRectGetMidY(b);
    CGContextSetTextPosition(c, round(px * SCALE) / SCALE, round(py * SCALE) / SCALE);
    CTLineDraw(line, c);
    CFRelease(line);
    CFRelease(str);
    CFRelease(attrs);
    CFRelease(font);
}

// The desktop's key in an outlined box.
static void draw_key(CGContextRef c, CGFloat x, char key) {
    CGRect box = CGRectMake(x + 0.5, (HEIGHT - STRIP_SLOT) / 2.0 + 0.5, STRIP_SLOT - 1, STRIP_SLOT - 1);
    CGPathRef path = CGPathCreateWithRoundedRect(box, 4.5, 4.5, NULL);
    CGContextSetLineWidth(c, 1);
    CGContextAddPath(c, path);
    CGContextStrokePath(c);
    CGPathRelease(path);

    CFStringRef text = CFStringCreateWithBytes(NULL, (const UInt8 *)&key, 1, kCFStringEncodingASCII, false);
    draw_centered(c, x, text, 13);
    CFRelease(text);
}

// The "+k" count of the windows that have no glyph.
static void draw_more(CGContextRef c, CGFloat x, int k) {
    char buf[8];
    snprintf(buf, sizeof buf, "+%d", k > 99 ? 99 : k);
    CFStringRef text = CFStringCreateWithCString(NULL, buf, kCFStringEncodingASCII);
    draw_centered(c, x, text, 11);
    CFRelease(text);
}

static void draw_app(CGContextRef c, CGRect slot, CGImageRef mask, bool focused) {
    CGRect inner = CGRectInset(slot, PAD, PAD);
    if (focused) {
        CGPathRef tile = CGPathCreateWithRoundedRect(slot, 5, 5, NULL);
        CGContextAddPath(c, tile);
        CGContextFillPath(c);
        CGPathRelease(tile);
        CGContextSetBlendMode(c, kCGBlendModeDestinationOut);
    }
    if (mask) CGContextDrawImage(c, inner, mask);
    CGContextSetBlendMode(c, kCGBlendModeNormal);
}

static void render(int ws, const pid_t *pids, const uint32_t *wids, int n, int over, uint32_t front) {
    int slots = strip_slots(n, over);
    int width = strip_width(reserve);
    int left = width - strip_width(slots); // the strip is right-aligned
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef c = CGBitmapContextCreate(NULL, width * SCALE, HEIGHT * SCALE, 8, 0, cs,
                                           (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(cs);
    if (!c) return;
    CGContextScaleCTM(c, SCALE, SCALE);
    CGContextSetInterpolationQuality(c, kCGInterpolationHigh);
    CGContextSetGrayFillColor(c, 1, 1);
    CGContextSetGrayStrokeColor(c, 1, 1);

    for (int i = 0; i < n; i++) {
        CGRect slot = CGRectMake(left + i * (STRIP_SLOT + STRIP_GAP), (HEIGHT - STRIP_SLOT) / 2.0, STRIP_SLOT, STRIP_SLOT);
        draw_app(c, slot, menubar_mask(pids[i]), wids[i] == front);
    }
    if (over > 0) draw_more(c, left + n * (STRIP_SLOT + STRIP_GAP), over);
    draw_key(c, left + slots * (STRIP_SLOT + STRIP_GAP), config_key_char(ws - 1));

    CGImageRef strip = CGBitmapContextCreateImage(c);
    CGContextRelease(c);
    NSImage *img = [[NSImage alloc] initWithCGImage:strip size:NSMakeSize(width, HEIGHT)];
    CGImageRelease(strip);
    img.template = YES;
    item.button.image = img;
}

// The last strip again, e.g. after the reserve shrank.
static void redraw(void) {
    @autoreleasepool {
        render(last_ws, last_pids, last_wids, last_n, last_over, last_front);
    }
}

void menubar_init(menubar_mode_t m) {
    mode = m;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    target = [JDClickTarget new];
    item = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
    // No permanent menu: it would swallow left clicks on the strip. It opens on a right or control click, and on any click in icon mode.
    item.button.target = target;
    item.button.action = @selector(clicked:);
    [item.button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
    if (mode == MB_ICON) item.button.image = icon_image();
}

// over_hash identifies the windows counted in over. n windows get a glyph (pids[i] owns wids[i]); over more are only counted. front is the
// focused window's id, 0 for none.
void menubar_update(int ws, const pid_t *pids, const uint32_t *wids, int n, int over, uint32_t over_hash, uint32_t front) {
    if (!item || mode != MB_STRIP) return;
    if (n > JD_MENUBAR_SLOTS) n = JD_MENUBAR_SLOTS;
    bool same_set = ws == last_ws && n == last_n && over == last_over && over_hash == last_hash &&
                    !memcmp(pids, last_pids, sizeof *pids * n) && !memcmp(wids, last_wids, sizeof *wids * n);
    int need = strip_slots(n, over);
    if (need != last_need) {
        last_need = need;
        reserve = strip_reserve(need, reserve, false);
        // Room that is no longer needed goes after STRIP_SHRINK_S; any change of need restarts that.
        unsigned gen = ++shrink_gen;
        if (reserve > strip_reserve(need, 0, true))
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, STRIP_SHRINK_S * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                if (gen != shrink_gen) return;
                reserve = strip_reserve(last_need, reserve, true);
                menubar_overflow_close(); // anchored to the old width
                redraw();
            });
    }
    if (same_set && front == last_front) return;
    if (!same_set) menubar_overflow_close(); // the list it shows is stale
    last_hash = over_hash;
    last_ws = ws;
    last_n = n;
    last_over = over;
    last_front = front;
    memcpy(last_pids, pids, sizeof *pids * n);
    memcpy(last_wids, wids, sizeof *wids * n);
    @autoreleasepool {
        render(ws, pids, wids, n, over, front);
    }
}

bool menubar_ready(void) {
    return item != nil && mode == MB_STRIP;
}
