#pragma once
// ~/.config/just-desktops/config, read once at start.
#include "common.h"

typedef enum { SW_OFF, SW_LIST, SW_SQUARE } sw_style_t;
typedef enum { MB_OFF, MB_STRIP, MB_ICON } menubar_mode_t;

void config_set_app_mode(bool app); // before config_load: the app defaults to strip and never has no item
void config_load(void);
#define CONFIG_EVERY (-1) // config_assign for an app set to `= none`: on every desktop, hidden when the desktop changes
int config_assign(const char *bundle_id); // desktop an app is pinned to, CONFIG_EVERY for `= none`, 0 when not pinned
bool config_no_preview(const char *bundle_id); // the square switcher never screenshots this app's windows
bool config_border(void);
sw_style_t config_switcher_style(void); // off: ⌥Tab cycles instantly with no panel
menubar_mode_t config_menubar(void); // strip: window glyphs; icon: only the app glyph; off: no item
CGEventFlags config_modifier(void); // base modifier flag of every hotkey: option (default), command or control
int config_workspaces(void); // number of desktops: how many keys are bound
int config_key(int i);       // virtual keycode of desktop i + 1
char config_key_char(int i); // that key as an uppercase character

// The value parsers, exposed for the tests.
bool config_parse_on(const char *v);
sw_style_t config_parse_switcher(const char *v); // "list"/"on", "square", "off" (instant, no panel); anything else is list
CGEventFlags config_parse_modifier(const char *v); // opt|option|alt, cmd|command, ctrl|control (any case); anything else is option
menubar_mode_t config_parse_menubar(const char *v); // "on"/"true"/"yes"/"strip", "icon"; anything else is off
int config_parse_keys(const char *list, int *codes); // up to JD_MAX_WS keycodes into codes; returns how many
