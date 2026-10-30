#import <AppKit/AppKit.h>
#include "menubar_priv.h"
#include "select.h"
#include "swkit.h"
#include "wm.h"

// The "+k" panel: one row per hidden window (icon, title) under a small arrow that points at
// the slot. Drawn by hand in a single view, so a row costs no subview. Created on click and
// released on close. (NSPopover is not used: on macOS 26 each one keeps a 5MB "RenderBox"
// surface alive after it closes.)
enum { ROW_H = 28, ROW_PAD = 6, ROW_ICON = 18, ROW_W = 280, MAX_ROWS = 14, ARROW = 7, CORNER = 10 };

static double overflow_closed_at;

@interface JDRows : NSView {
    @public
    sw_entry_t *rows;
    int n, sel, top;
    CGFloat arrow_x; // where the arrow points, in view coordinates
}
@end

@implementation JDRows
- (instancetype)initWithRows:(sw_entry_t *)r count:(int)count {
    int vis = count < MAX_ROWS ? count : MAX_ROWS;
    if (!(self = [super initWithFrame:NSMakeRect(0, 0, ROW_W, vis * ROW_H + 2 * ROW_PAD + ARROW)])) return nil;
    rows = r;
    n = count;
    return self;
}
- (void)dealloc {
    sw_free_entries(rows, n);
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { (void)e; return YES; }
- (int)visible { return n < MAX_ROWS ? n : MAX_ROWS; }
- (void)select:(int)i {
    sel = sel_wrap(i, n);
    top = sel_top(top, sel, [self visible]);
    [self setNeedsDisplay:YES];
}
- (void)choose:(int)i {
    uint32_t wid = rows[i].wid;
    menubar_overflow_close();
    wm_set_trigger("menubar");
    wm_focus_wid(wid);
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    NSRect body = NSMakeRect(0.5, ARROW + 0.5, ROW_W - 1, self.bounds.size.height - ARROW - 1);
    NSBezierPath *bubble = [NSBezierPath bezierPathWithRoundedRect:body xRadius:CORNER yRadius:CORNER];
    CGFloat ax = MAX(CORNER + ARROW, MIN(ROW_W - CORNER - ARROW, arrow_x));
    [bubble moveToPoint:NSMakePoint(ax - ARROW, ARROW + 0.5)];
    [bubble lineToPoint:NSMakePoint(ax, 0.5)];
    [bubble lineToPoint:NSMakePoint(ax + ARROW, ARROW + 0.5)];
    [bubble closePath];
    [NSColor.windowBackgroundColor setFill];
    [bubble fill];
    [NSColor.separatorColor setStroke];
    [bubble stroke];

    NSMutableParagraphStyle *ps = [NSMutableParagraphStyle new];
    ps.lineBreakMode = NSLineBreakByTruncatingTail;
    for (int k = 0; k < [self visible]; k++) {
        int i = top + k;
        NSRect r = NSMakeRect(ROW_PAD, ARROW + ROW_PAD + k * ROW_H, ROW_W - 2 * ROW_PAD, ROW_H);
        if (i == sel) {
            [NSColor.selectedContentBackgroundColor setFill];
            [[NSBezierPath bezierPathWithRoundedRect:r xRadius:5 yRadius:5] fill];
        }
        CGFloat x = r.origin.x + 6;
        if (rows[i].icon) {
            NSRect ir = NSMakeRect(x, r.origin.y + (ROW_H - ROW_ICON) / 2, ROW_ICON, ROW_ICON);
            NSImage *img = [[NSImage alloc] initWithCGImage:rows[i].icon size:NSMakeSize(ROW_ICON, ROW_ICON)];
            [img drawInRect:ir];
        }
        NSString *t = rows[i].title ? (__bridge NSString *)rows[i].title : @"Window";
        NSDictionary *at = @{
            NSFontAttributeName : [NSFont systemFontOfSize:13],
            NSForegroundColorAttributeName : i == sel ? NSColor.alternateSelectedControlTextColor : NSColor.labelColor,
            NSParagraphStyleAttributeName : ps,
        };
        CGFloat tx = x + ROW_ICON + 8, th = [t sizeWithAttributes:at].height;
        [t drawInRect:NSMakeRect(tx, r.origin.y + (ROW_H - th) / 2, r.origin.x + r.size.width - 6 - tx, th) withAttributes:at];
    }
}
- (int)rowAt:(NSEvent *)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    return sel_row_at(p.y - ARROW - ROW_PAD, ROW_H, top, [self visible]);
}
- (void)mouseMoved:(NSEvent *)e {
    int i = [self rowAt:e];
    if (i >= 0 && i != sel) [self select:i];
}
- (void)mouseUp:(NSEvent *)e {
    int i = [self rowAt:e];
    if (i >= 0) [self choose:i];
}
- (void)scrollWheel:(NSEvent *)e {
    int most = n - [self visible];
    if (most <= 0 || e.scrollingDeltaY == 0) return;
    top = sel_clamp(top + (e.scrollingDeltaY < 0 ? 1 : -1), 0, most);
    [self setNeedsDisplay:YES];
}
- (void)updateTrackingAreas {
    for (NSTrackingArea *a in self.trackingAreas) [self removeTrackingArea:a];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:self.bounds
                                                       options:NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect
                                                         owner:self userInfo:nil]];
    [super updateTrackingAreas];
}
- (void)keyDown:(NSEvent *)e {
    BOOL shift = e.modifierFlags & NSEventModifierFlagShift;
    switch (e.keyCode) {
    case 125: [self select:sel + 1]; break;                  // down
    case 126: [self select:sel - 1]; break;                  // up
    case 48: [self select:sel + (shift ? -1 : 1)]; break;    // tab
    case 36: case 76: [self choose:sel]; break;              // return, enter
    case 53: menubar_overflow_close(); break;                    // esc
    default: break;
    }
}
@end

@interface JDPanel : NSPanel <NSWindowDelegate>
@end

@implementation JDPanel
- (BOOL)canBecomeKeyWindow { return YES; }
- (void)windowDidResignKey:(NSNotification *)note {
    (void)note;
    menubar_overflow_close(); // a click elsewhere
}
@end

static JDPanel *panel;

void menubar_overflow_close(void) {
    if (!panel) return;
    JDPanel *p = panel;
    panel = nil;
    overflow_closed_at = CFAbsoluteTimeGetCurrent();
    p.delegate = nil;
    [p orderOut:nil];
    // The click that closes it may be running inside the view: drop the view on the next turn, not now.
    dispatch_async(dispatch_get_main_queue(), ^{ p.contentView = nil; });
}

void menubar_overflow_open(NSButton *b, NSRect slot) {
    sw_entry_t *rows = NULL;
    int n = wm_overflow(&rows);
    if (n <= 0) return;
    JDRows *view = [[JDRows alloc] initWithRows:rows count:n];
    NSRect at = [b.window convertRectToScreen:[b convertRect:slot toView:nil]];
    NSRect vis = b.window.screen.visibleFrame;
    NSSize size = view.frame.size;
    CGFloat x = MAX(NSMinX(vis) + 4, MIN(NSMaxX(vis) - size.width - 4, NSMidX(at) - size.width / 2));
    view->arrow_x = NSMidX(at) - x;
    panel = [[JDPanel alloc] initWithContentRect:NSMakeRect(x, NSMinY(at) - 2 - size.height, size.width, size.height)
                                        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    panel.releasedWhenClosed = NO;
    panel.opaque = NO;
    panel.backgroundColor = NSColor.clearColor;
    panel.hasShadow = YES;
    panel.level = NSPopUpMenuWindowLevel;
    panel.delegate = panel;
    panel.contentView = view;
    [panel makeKeyAndOrderFront:nil];
    [panel makeFirstResponder:view];
}

bool menubar_overflow_is_open(void) {
    return panel != nil;
}

bool menubar_overflow_just_closed(void) {
    return CFAbsoluteTimeGetCurrent() - overflow_closed_at < 0.4;
}
