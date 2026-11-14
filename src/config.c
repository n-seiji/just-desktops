#include "config.h"
#include "config_write.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// ~/.config/just-desktops/config: one `key = value` per line.
//   # starts a comment, anywhere on a line. Values are case-insensitive. The last line for a key wins.
//   An unknown value, key or line is ignored with a warning on stderr (the setting keeps its earlier or default value).
//   border = on|off
//   switcher = list|square|off   (⌥Tab window switcher; on = list; off = switch at once, no panel)
//   modifier = opt|cmd|ctrl   (base modifier of every hotkey, default opt; aliases option, alt, command, control)
//   menubar = on|strip|icon|off (menu bar item: on/strip = windows and the desktop key, icon = app glyph only; the app treats off as icon)
//   keys = 1 2 3 q w e    (digits and letters; the n-th key is Desktop n)
//   no_preview = <bundle id> [<bundle id> ...]   (apps the square switcher never screenshots; repeatable)
//   <bundle id> = <desktop number, 1-10> | none   (none: the app is on every desktop, and is hidden when you switch desktops)

typedef struct { char id[128]; int ws; } assign_t;

static assign_t *assigns;
static int nassigns;
typedef struct { char id[128]; } nopreview_t;
static nopreview_t *extra; // no_preview entries from the config file, on top of the built-in list
static int nextra;
static bool border = true;
static sw_style_t switcher = SW_LIST;
static menubar_mode_t menubar = MB_OFF;
static bool app_mode;
static CGEventFlags modifier = kCGEventFlagMaskAlternate;
#define DEFAULT_KEYS {0x12, 0x13, 0x14, 0x0C, 0x0D, 0x0E} // 1 2 3 q w e
static int keys[JD_MAX_WS] = DEFAULT_KEYS;
static int nkeys = 6;

// Password managers: their windows are never captured for the square switcher (ids compared case-insensitively).
static const char *const secret_apps[] = {
    "com.1password.1password", "com.agilebits.onepassword7", "com.agilebits.onepassword-osx", "com.agilebits.onepassword4",
    "com.bitwarden.desktop", "com.dashlane.dashlanephonefinal", "com.lastpass.lastpassmacdesktop",
    "org.keepassxc.keepassxc", "com.keepassium.ios", "com.markmcguill.strongbox.mac",
    "com.markmcguill.strongbox.mac.mas", "in.sinew.Enpass-Desktop", "io.enpass.Enpass", "com.nordsec.nordpass",
    "me.proton.pass.electron", "com.callpod.keepermac.lite", "com.apple.Passwords", "com.apple.keychainaccess",
    "com.hicknhacksoftware.MacPass", "com.buttercup.desktop",
};

// ANSI virtual keycode -> character; a space marks a keycode that is not bindable.
static const char layout[] = "asdfhgzxcv bqweryt123465 97 80 ou ip lj k    nm";

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

// Case-insensitive. True and *out set for on|off; anything else leaves *out alone.
bool config_parse_bool(const char *v, bool *out) {
    if (!strcasecmp(v, "on")) *out = true;
    else if (!strcasecmp(v, "off")) *out = false;
    else return false;
    return true;
}

// Parses space-separated single characters into virtual keycodes; tokens that are not a single digit or
// letter, repeats and tokens beyond JD_MAX_WS are skipped and counted in *skipped (may be NULL).
// Returns the number written to codes (at most JD_MAX_WS).
int config_parse_keys(const char *list, int *codes, int *skipped) {
    int n = 0, skip = 0;
    for (const char *p = list; *p;) {
        if (isspace((unsigned char)*p)) { p++; continue; }
        const char *e = p;
        while (*e && !isspace((unsigned char)*e)) e++;
        const char *at = e - p == 1 ? strchr(layout, tolower((unsigned char)*p)) : NULL;
        int code = at ? (int)(at - layout) : -1;
        for (int i = 0; i < n && code >= 0; i++)
            if (codes[i] == code) code = -1;
        if (code >= 0 && n < JD_MAX_WS) codes[n++] = code;
        else skip++;
        p = e;
    }
    if (skipped) *skipped = skip;
    return n;
}

// Case-insensitive: list|on, square, off (the instant mode without a panel).
bool config_parse_switcher(const char *v, sw_style_t *out) {
    if (!strcasecmp(v, "list") || !strcasecmp(v, "on")) *out = SW_LIST;
    else if (!strcasecmp(v, "square")) *out = SW_SQUARE;
    else if (!strcasecmp(v, "off")) *out = SW_OFF;
    else return false;
    return true;
}

// Case-insensitive.
bool config_parse_modifier(const char *v, CGEventFlags *out) {
    if (!strcasecmp(v, "opt") || !strcasecmp(v, "option") || !strcasecmp(v, "alt")) *out = kCGEventFlagMaskAlternate;
    else if (!strcasecmp(v, "cmd") || !strcasecmp(v, "command")) *out = kCGEventFlagMaskCommand;
    else if (!strcasecmp(v, "ctrl") || !strcasecmp(v, "control")) *out = kCGEventFlagMaskControl;
    else return false;
    return true;
}

// Case-insensitive: on|strip, icon, off.
bool config_parse_menubar(const char *v, menubar_mode_t *out) {
    if (!strcasecmp(v, "on") || !strcasecmp(v, "strip")) *out = MB_STRIP;
    else if (!strcasecmp(v, "icon")) *out = MB_ICON;
    else if (!strcasecmp(v, "off")) *out = MB_OFF;
    else return false;
    return true;
}

void config_set_app_mode(bool app) { app_mode = app; }

static void warn_value(const char *k, const char *v) { fprintf(stderr, "just-desktops: config: unknown value for %s ignored: %s\n", k, v); }

// Back to the defaults, so that every load reads the file alone.
static void reset(void) {
    free(assigns);
    assigns = NULL;
    nassigns = 0;
    free(extra);
    extra = NULL;
    nextra = 0;
    border = true;
    switcher = SW_LIST;
    modifier = kCGEventFlagMaskAlternate;
    static const int def[] = DEFAULT_KEYS;
    memcpy(keys, def, sizeof def);
    nkeys = 6;
    menubar = app_mode ? MB_STRIP : MB_OFF;
}

void config_load(void) {
    reset();
    char path[1024];
    config_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;

    // Lines have no length limit: a long no_preview list must not lose its tail, because every id lost is an app that gets screenshotted.
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) >= 0) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *s = trim(line);
        if (!*s) continue;
        char *eq = strchr(s, '=');
        if (!eq || eq == s) {
            fprintf(stderr, "just-desktops: config: unknown line ignored: %s\n", s);
            continue;
        }
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);

        if (!strcmp(k, "border")) {
            if (!config_parse_bool(v, &border)) warn_value(k, v);
            continue;
        }
        if (!strcmp(k, "switcher")) {
            if (!config_parse_switcher(v, &switcher)) warn_value(k, v);
            continue;
        }
        if (!strcmp(k, "modifier")) {
            if (!config_parse_modifier(v, &modifier)) warn_value(k, v);
            continue;
        }
        if (!strcmp(k, "menubar")) {
            if (!config_parse_menubar(v, &menubar)) warn_value(k, v);
            continue;
        }
        if (!strcmp(k, "keys")) {
            int parsed[JD_MAX_WS], skipped;
            int n = config_parse_keys(v, parsed, &skipped);
            if (n) {
                memcpy(keys, parsed, sizeof *parsed * (size_t)n);
                nkeys = n;
            }
            if (!n) fprintf(stderr, "just-desktops: config: keys = %s ignored: no bindable key (digits and letters)\n", v);
            else if (skipped) fprintf(stderr, "just-desktops: config: keys = %s: %d token(s) skipped (not a single digit or letter, repeated, or beyond %d)\n", v, skipped, JD_MAX_WS);
            continue;
        }
        if (!strcmp(k, "no_preview")) {
            for (char *tok = strtok(v, " \t"); tok; tok = strtok(NULL, " \t")) {
                if (strlen(tok) >= sizeof extra->id) {
                    fprintf(stderr, "just-desktops: config: no_preview id longer than %zu bytes ignored: %.20s...\n", sizeof extra->id - 1, tok);
                    continue;
                }
                nopreview_t *n = realloc(extra, sizeof *extra * (nextra + 1));
                if (!n) break;
                extra = n;
                snprintf(extra[nextra++].id, sizeof extra->id, "%s", tok);
            }
            continue;
        }

        // Anything else is a bundle id: none, or a whole number from 1 to JD_MAX_WS.
        int ws = CONFIG_EVERY;
        if (strcasecmp(v, "none")) {
            char *end = v;
            long num = isdigit((unsigned char)*v) ? strtol(v, &end, 10) : 0;
            if (num < 1 || num > JD_MAX_WS || *end) {
                fprintf(stderr, "just-desktops: config: %s = %s ignored: not a known setting, and not a desktop number (1-%d) or none\n", k, v, JD_MAX_WS);
                continue;
            }
            ws = (int)num;
        }
        if (strlen(k) >= sizeof assigns->id) {
            fprintf(stderr, "just-desktops: config: bundle id longer than %zu bytes ignored: %.20s...\n", sizeof assigns->id - 1, k);
            continue;
        }
        int at = 0;
        while (at < nassigns && strcmp(assigns[at].id, k)) at++; // the last line for an app wins
        if (at == nassigns) {
            assign_t *n = realloc(assigns, sizeof *assigns * (nassigns + 1));
            if (!n) break;
            assigns = n;
            snprintf(assigns[nassigns++].id, sizeof assigns->id, "%s", k);
        }
        assigns[at].ws = ws;
    }
    free(line);
    fclose(f);
    if (app_mode && menubar == MB_OFF) menubar = MB_ICON;
}

int config_assign(const char *bundle_id) {
    if (!bundle_id) return 0;
    for (int i = 0; i < nassigns; i++)
        if (!strcmp(assigns[i].id, bundle_id)) {
            int ws = assigns[i].ws;
            if (ws == CONFIG_EVERY) return CONFIG_EVERY;
            return ws >= 1 && ws <= nkeys ? ws : 0;
        }
    return 0;
}

bool config_no_preview(const char *bundle_id) {
    if (!bundle_id) return false;
    for (size_t i = 0; i < sizeof secret_apps / sizeof *secret_apps; i++)
        if (!strcasecmp(secret_apps[i], bundle_id)) return true;
    for (int i = 0; i < nextra; i++)
        if (!strcasecmp(extra[i].id, bundle_id)) return true;
    return false;
}

bool config_border(void) { return border; }

sw_style_t config_switcher_style(void) { return switcher; }

menubar_mode_t config_menubar(void) { return menubar; }

CGEventFlags config_modifier(void) { return modifier; }

int config_workspaces(void) { return nkeys; }

int config_key(int i) { return keys[i]; }

char config_key_char(int i) { return toupper((unsigned char)layout[keys[i]]); }
