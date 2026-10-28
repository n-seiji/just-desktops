#pragma once
// Types and limits that more than one module uses.
#include <ApplicationServices/ApplicationServices.h>
#include <stdbool.h>
#include <stdint.h>

#define JD_VERSION "0.1.6"
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
