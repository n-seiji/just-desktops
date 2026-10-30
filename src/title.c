#include "title.h"
#include <stdlib.h>

// Window titles often end in " - <app name>" (browsers add a profile after it too).
// Strip that tail so the title alone is the primary text. NULL means app-only.

static bool is_sep(const UniChar *s, CFIndex i, CFIndex len) {
    return i + 2 < len && s[i] == ' ' && s[i + 2] == ' ' && (s[i + 1] == '-' || s[i + 1] == 0x2014 || s[i + 1] == 0x2013);
}

static bool is_space(UniChar c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x3000 || c == 0xA0;
}

static CFRange trimmed(const UniChar *s, CFIndex from, CFIndex to) {
    while (from < to && is_space(s[from])) from++;
    while (to > from && is_space(s[to - 1])) to--;
    return CFRangeMake(from, to - from);
}

static bool is_app(CFStringRef str, CFStringRef app, CFRange r) {
    CFRange all = CFRangeMake(0, CFStringGetLength(app));
    return r.length == all.length && CFStringCompareWithOptions(str, app, r, kCFCompareCaseInsensitive) == kCFCompareEqualTo;
}

CFStringRef title_clean(CFStringRef title, CFStringRef app) {
    CFIndex len = title ? CFStringGetLength(title) : 0;
    UniChar *s = malloc(sizeof *s * (len ? len : 1));
    if (!s) return NULL;
    if (len) CFStringGetCharacters(title, CFRangeMake(0, len), s);

    CFIndex end = len, start = 0;
    for (CFIndex i = 0; app && i <= len; i++) {
        bool sep = i < len && is_sep(s, i, len);
        if (i < len && !sep) continue;
        if (start > 0 && is_app(title, app, trimmed(s, start, i))) {
            end = start - 3;
            break;
        }
        start = i + 3;
        i += 2;
    }

    CFRange r = trimmed(s, 0, end);
    free(s);
    if (!r.length || (app && is_app(title, app, r))) return NULL;
    return CFStringCreateWithSubstring(NULL, title, r);
}

void title_labels(CFStringRef app, const CFStringRef *titles, int n, CFStringRef *out) {
    for (int i = 0; i < n; i++) {
        if (n == 1) out[i] = CFRetain(app);
        else if (titles[i]) out[i] = CFRetain(titles[i]);
        else out[i] = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@ %d"), app, i + 1);
        for (int j = 0; j < i; j++) {
            if (!CFEqual(out[j], out[i])) continue;
            CFStringRef numbered = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@ %d"), out[i], i + 1);
            CFRelease(out[i]);
            out[i] = numbered;
            break;
        }
    }
}
