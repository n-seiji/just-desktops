#include "swkit.h"
#include "appearance.h"
#include "skylight.h"
#include <math.h>
#include <stdlib.h>

static const palette_t DARK = {0x141416, 0.85, 1, 0.35, 0.18, 0x9a9a9e, 0xffffff, 0xffffff, 0.10};
static const palette_t LIGHT = {0xf6f6f8, 0.80, 0, 0.10, 0.16, 0x6e6e73, 0x1d1d1f, 0x000000, 0.08};

const palette_t *sw_palette(void) {
    return appearance_dark() ? &DARK : &LIGHT;
}

static const CGFloat FONT_WEIGHT = 0.23;

CTFontRef sw_font(CGFloat sz) {
    CTFontRef base = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, sz, NULL);
    if (!base) return NULL;
    CFNumberRef w = CFNumberCreate(NULL, kCFNumberCGFloatType, &FONT_WEIGHT);
    CFDictionaryRef traits = CFDictionaryCreate(NULL, (const void **)&kCTFontWeightTrait, (const void **)&w, 1,
                                                &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, (const void **)&kCTFontTraitsAttribute, (const void **)&traits, 1,
                                               &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CTFontDescriptorRef d = CTFontDescriptorCreateWithAttributes(attrs);
    CTFontRef f = CTFontCreateCopyWithAttributes(base, sz, NULL, d);
    CFRelease(d);
    CFRelease(attrs);
    CFRelease(traits);
    CFRelease(w);
    CFRelease(base);
    return f ? f : CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, sz, NULL);
}

void sw_color(CGContextRef c, uint32_t rgb, CGFloat a, bool stroke) {
    CGFloat r = (rgb >> 16 & 255) / 255.0, g = (rgb >> 8 & 255) / 255.0, b = (rgb & 255) / 255.0;
    if (stroke) CGContextSetRGBStrokeColor(c, r, g, b, a);
    else CGContextSetRGBFillColor(c, r, g, b, a);
}

// One line of text, cut with an ellipsis to maxw (at the end, or at the start for paths).
CTLineRef sw_fit(CTFontRef font, CFStringRef s, uint32_t rgb, CGFloat maxw, CTLineTruncationType how) {
    CGColorRef col = CGColorCreateSRGB((rgb >> 16 & 255) / 255.0, (rgb >> 8 & 255) / 255.0, (rgb & 255) / 255.0, 1);
    CFStringRef keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
    CFTypeRef vals[] = {font, col};
    CFDictionaryRef at = CFDictionaryCreate(NULL, (const void **)keys, (const void **)vals, 2,
                                            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(NULL, s, at);
    CTLineRef line = CTLineCreateWithAttributedString(as);
    if (CTLineGetTypographicBounds(line, NULL, NULL, NULL) > maxw) {
        CFAttributedStringRef dots = CFAttributedStringCreate(NULL, CFSTR("\xE2\x80\xA6"), at);
        CTLineRef token = CTLineCreateWithAttributedString(dots);
        CTLineRef cut = CTLineCreateTruncatedLine(line, maxw, how, token);
        CFRelease(token);
        CFRelease(dots);
        if (cut) {
            CFRelease(line);
            line = cut;
        }
    }
    CFRelease(as);
    CFRelease(at);
    CGColorRelease(col);
    return line;
}

// Draws l with its x-height-ish middle on cy, then releases it. The context is y-down.
void sw_put(CGContextRef c, CTLineRef l, CGFloat x, CGFloat cy) {
    CGFloat asc, desc;
    CTLineGetTypographicBounds(l, &asc, &desc, NULL);
    CGContextSetTextPosition(c, x, cy + (asc - desc) / 2);
    CTLineDraw(l, c);
    CFRelease(l);
}

static CGPathRef rrect(CGFloat x, CGFloat y, CGFloat w, CGFloat h, CGFloat r) {
    return CGPathCreateWithRoundedRect((CGRect){{x, y}, {w, h}}, r, r, NULL);
}

void sw_fill_rrect(CGContextRef c, CGRect r, CGFloat radius, uint32_t rgb, CGFloat a) {
    CGPathRef p = rrect(r.origin.x, r.origin.y, r.size.width, r.size.height, radius);
    sw_color(c, rgb, a, false);
    CGContextAddPath(c, p);
    CGContextFillPath(c);
    CGPathRelease(p);
}

// A 1pt border drawn inside r, lit along the top and fading to a flat rim.
void sw_edge_rrect(CGContextRef c, CGRect r, CGFloat radius, const palette_t *pal) {
    CGPathRef p = rrect(r.origin.x + 0.5, r.origin.y + 0.5, r.size.width - 1, r.size.height - 1, radius - 0.5);
    CGContextSaveGState(c);
    CGContextSetLineWidth(c, 1);
    CGContextAddPath(c, p);
    CGContextReplacePathWithStrokedPath(c);
    CGContextClip(c);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGFloat g0 = pal->edge_gray;
    CGFloat comps[8] = {g0, g0, g0, pal->edge_top, g0, g0, g0, pal->edge_bottom};
    CGGradientRef g = CGGradientCreateWithColorComponents(cs, comps, NULL, 2);
    CGContextDrawLinearGradient(c, g, CGPointMake(0, r.origin.y), CGPointMake(0, r.origin.y + radius * 1.5),
                                kCGGradientDrawsAfterEndLocation);
    CGGradientRelease(g);
    CGColorSpaceRelease(cs);
    CGContextRestoreGState(c);
    CGPathRelease(p);
}

bool sw_is_path(CFStringRef s) {
    return CFStringHasPrefix(s, CFSTR("~/")) || CFStringHasPrefix(s, CFSTR("/"));
}

uint32_t sw_mix(uint32_t fg, uint32_t bg, CGFloat t) {
    uint32_t out = 0;
    for (int sh = 16; sh >= 0; sh -= 8)
        out |= (uint32_t)lround(((fg >> sh & 255) * t) + ((bg >> sh & 255) * (1 - t))) << sh;
    return out;
}

// The panel's shape: horizontal half-point strips approximating the rounded rect, so the
// background blur does not show outside the corners.
static CFTypeRef rounded_region(CGSize sz, CGFloat radius) {
    HIMutableShapeRef shape = HIShapeCreateMutable();
    int steps = (int)(radius * 2);
    for (int k = 0; k < steps; k++) {
        CGFloat dy = radius - (k + 0.5) / 2, dx = radius - sqrt(radius * radius - dy * dy);
        CGRect top = {{dx, k / 2.0}, {sz.width - 2 * dx, 0.5}};
        CGRect bot = {{dx, sz.height - (k + 1) / 2.0}, {sz.width - 2 * dx, 0.5}};
        HIShapeUnionWithRect(shape, &top);
        HIShapeUnionWithRect(shape, &bot);
    }
    CGRect mid = {{0, radius}, {sz.width, sz.height - 2 * radius}};
    HIShapeUnionWithRect(shape, &mid);
    return shape;
}

void sw_close_window(int cid, uint32_t *wid, CGContextRef *ctx) {
    if (!*wid) return;
    SLSOrderWindow(cid, *wid, 0, 0);
    if (*ctx) CGContextRelease(*ctx);
    SLSReleaseWindow(cid, *wid);
    *wid = 0;
    *ctx = NULL;
}

void sw_timer_start(dispatch_source_t *t, void (*fn)(void)) {
    if (*t) return;
    *t = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_timer(*t, DISPATCH_TIME_NOW, NSEC_PER_SEC / 60, NSEC_PER_MSEC);
    dispatch_source_set_event_handler(*t, ^{ fn(); });
    dispatch_resume(*t);
}

void sw_timer_stop(dispatch_source_t *t) {
    if (!*t) return;
    dispatch_source_cancel(*t);
    dispatch_release(*t);
    *t = NULL;
}

uint32_t sw_window(int cid, CGPoint at, CGSize sz, CGFloat radius, int blur, double res, CGContextRef *out) {
    CGRect r = {{0, 0}, sz};
    CFTypeRef region = NULL;
    CGSNewRegionWithRect(&r, &region);
    if (!region) return 0;
    uint32_t w = 0;
    SLSNewWindow(cid, 2, at.x, at.y, region, &w);
    CFRelease(region);
    if (!w) return 0;
    uint64_t tags[2] = {(1ULL << 1) | (1ULL << 9), 0}; // floating, ignores mouse events
    SLSSetWindowTags(cid, w, tags, 64);
    SLSSetWindowResolution(cid, w, res);
    SLSSetWindowOpacity(cid, w, false);
    SLSSetWindowLevel(cid, w, CGWindowLevelForKey(kCGPopUpMenuWindowLevelKey));
    if (radius > 0) {
        CFTypeRef shape = rounded_region(sz, radius);
        SLSSetWindowShape(cid, w, 0, 0, shape);
        CFRelease(shape);
    }
    if (blur) SLSSetWindowBackgroundBlurRadius(cid, w, blur);
    *out = SLWindowContextCreate(cid, w, NULL);
    return w;
}

void sw_free_entries(sw_entry_t *ents, int n) {
    for (int i = 0; i < n; i++) {
        if (ents[i].icon) CGImageRelease(ents[i].icon);
        if (ents[i].app) CFRelease(ents[i].app);
        if (ents[i].title) CFRelease(ents[i].title);
    }
    free(ents);
}
