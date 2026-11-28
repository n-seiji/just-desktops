#pragma once
// The version, the limits, WS_EVERY, sw_entry_t and sw_free_entries, shared across modules.
#include <ApplicationServices/ApplicationServices.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define JD_VERSION "0.1.12"
#define JD_MAX_WS 10 // most desktops `keys` can bind
#define JD_MENUBAR_SLOTS 10 // menu bar glyph slots: with more windows the last slot is a "+k" count
#define JD_MENUBAR_WINS 64  // windows the menu bar looks at
#define WS_EVERY 0 // win_t.ws of a window on every desktop (assign CONFIG_EVERY)

// A window as the switchers list it. icon, app and title are +1 retained and released by the owner.
typedef struct {
    uint32_t wid;
    pid_t pid;
    CGImageRef icon;
    CFStringRef app, title;
    bool nopreview; // never capture this window (password managers)
} sw_entry_t;

// Releases the entries wm_windows or wm_overflow returned.
static inline void sw_free_entries(sw_entry_t *ents, int n) {
    for (int i = 0; i < n; i++) {
        CGImageRelease(ents[i].icon);
        if (ents[i].app) CFRelease(ents[i].app);
        if (ents[i].title) CFRelease(ents[i].title);
    }
    free(ents);
}
