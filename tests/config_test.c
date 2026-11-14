#include "../src/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static int failed;

static void check(const char *list, int n, const int *want, int want_skipped) {
    int got[JD_MAX_WS + 4], skipped = -1;
    int c = config_parse_keys(list, got, &skipped);
    bool ok = c == n && skipped == want_skipped;
    for (int i = 0; ok && i < n; i++) ok = got[i] == want[i];
    if (!ok) {
        printf("FAIL config_parse_keys(\"%s\") returned %d, skipped %d; want %d, skipped %d\n", list, c, skipped, n, want_skipped);
        failed++;
    }
}

// want < 0: the value must be rejected and *out left alone.
static void check_bool(const char *v, int want) {
    for (int init = 0; init < 2; init++) { // an invalid value must leave out alone, whatever it held
        bool out = init;
        bool ok = config_parse_bool(v, &out);
        if (ok == (want >= 0) && out == (ok ? want : init)) continue;
        printf("FAIL config_parse_bool(\"%s\") with out %d = %d, out %d; want %s\n", v, init, ok, out, want < 0 ? "invalid, out unchanged" : want ? "valid, 1" : "valid, 0");
        failed++;
    }
}

static void check_switcher(const char *v, int want) {
    sw_style_t out = (sw_style_t)99;
    bool ok = config_parse_switcher(v, &out);
    if (ok != (want >= 0) || (ok && (int)out != want) || (!ok && out != 99)) {
        printf("FAIL config_parse_switcher(\"%s\") = %d, out %d; want %d (-1: invalid, out untouched)\n", v, ok, out, want);
        failed++;
    }
}

static void check_menubar(const char *v, int want) {
    menubar_mode_t out = (menubar_mode_t)99;
    bool ok = config_parse_menubar(v, &out);
    if (ok != (want >= 0) || (ok && (int)out != want) || (!ok && out != 99)) {
        printf("FAIL config_parse_menubar(\"%s\") = %d, out %d; want %d (-1: invalid, out untouched)\n", v, ok, out, want);
        failed++;
    }
}

static void check_modifier(const char *v, long long want) {
    CGEventFlags out = 99;
    bool ok = config_parse_modifier(v, &out);
    if (ok != (want >= 0) || (ok && (long long)out != want) || (!ok && out != 99)) {
        printf("FAIL config_parse_modifier(\"%s\") = %d, out %llx; want %llx (-1: invalid, out untouched)\n", v, ok, (unsigned long long)out, (unsigned long long)want);
        failed++;
    }
}

// Every letter and digit must reach the kVK_ANSI_* code of that key.
static void check_layout(void) {
    static const struct { char c; int code; } want[] = {
        {'a', 0x00}, {'s', 0x01}, {'d', 0x02}, {'f', 0x03}, {'h', 0x04}, {'g', 0x05}, {'z', 0x06}, {'x', 0x07}, {'c', 0x08}, {'v', 0x09},
        {'b', 0x0B}, {'q', 0x0C}, {'w', 0x0D}, {'e', 0x0E}, {'r', 0x0F}, {'y', 0x10}, {'t', 0x11},
        {'1', 0x12}, {'2', 0x13}, {'3', 0x14}, {'4', 0x15}, {'6', 0x16}, {'5', 0x17}, {'9', 0x19}, {'7', 0x1A}, {'8', 0x1C}, {'0', 0x1D},
        {'o', 0x1F}, {'u', 0x20}, {'i', 0x22}, {'p', 0x23}, {'l', 0x25}, {'j', 0x26}, {'k', 0x28}, {'n', 0x2D}, {'m', 0x2E},
    };
    for (size_t i = 0; i < sizeof want / sizeof *want; i++) {
        char tok[2] = {want[i].c, 0};
        int got = -1, skipped = 0;
        int n = config_parse_keys(tok, &got, &skipped);
        if (n != 1 || got != want[i].code) {
            printf("FAIL config_parse_keys(\"%s\") = %d (n %d); want 0x%x\n", tok, got, n, want[i].code);
            failed++;
        }
    }
}

// Writes text as the config file of a fresh HOME and loads it (config_load resets every setting first).
static void load_text(const char *text) {
    char home[] = "/tmp/jd_config_txt.XXXXXX", cmd[256], path[256];
    if (!mkdtemp(home)) exit(1);
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/.config/just-desktops'", home);
    if (system(cmd)) exit(1);
    snprintf(path, sizeof path, "%s/.config/just-desktops/config", home);
    FILE *f = fopen(path, "w");
    if (!f) exit(1);
    fputs(text, f);
    fclose(f);
    setenv("HOME", home, 1);
    config_set_app_mode(false);
    config_load();
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", home);
    if (system(cmd)) failed++;
}

static void check_key_char(int i, char want) {
    char got = config_key_char(i);
    if (got != want) {
        printf("FAIL config_key_char(%d) = '%c'; want '%c'\n", i, got, want);
        failed++;
    }
}

static void check_pin(const char *id, int want_ws) {
    int ws = config_assign(id);
    if (ws != want_ws) {
        printf("FAIL config_assign(\"%s\") = %d; want %d\n", id, ws, want_ws);
        failed++;
    }
}

static void check_no_preview(const char *id, bool want) {
    if (config_no_preview(id) != want) {
        printf("FAIL config_no_preview(\"%s\") = %d; want %d\n", id, !want, want);
        failed++;
    }
}

int main(void) {
    if (config_modifier() != kCGEventFlagMaskAlternate) {
        printf("FAIL default config_modifier() = %llx; want option\n", (unsigned long long)config_modifier());
        failed++;
    }
    check_no_preview("com.1password.1password", true);
    check_no_preview("COM.Apple.Passwords", true);
    check_no_preview("com.apple.keychainaccess", true);
    check_no_preview("org.keepassxc.keepassxc", true);
    check_no_preview("com.apple.Safari", false);
    check_no_preview("", false);
    check_no_preview(NULL, false);

    char dir[] = "/tmp/jd_config_test.XXXXXX";
    if (!mkdtemp(dir)) return 1;
    char path[256];
    snprintf(path, sizeof path, "%s/.config", dir);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/.config/just-desktops", dir);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/.config/just-desktops/config", dir);
    FILE *f = fopen(path, "w");
    if (!f) return 1;
    fputs("no_preview = com.example.vault  com.example.notes\nno_preview=Com.Example.Other\nmodifier = CMD\ncom.example.pinned = 2\n", f);
    fputs("no_preview =", f); // a line far over 512 bytes: every id on it counts, the last one too
    for (int i = 0; i < 400; i++) fprintf(f, " com.example.long%d", i);
    fprintf(f, " com.example.%0150d com.example.after\n", 7); // one id too long for the buffer, then one more
    fputs("modifier = ctrl\n", f); // read after the long line: it must not have been split
    fputs("com.example.floating = none\ncom.example.caps = NONE\ncom.example.bogus = nonex\n", f);
    fputs("com.example.tight=none\ncom.example.note = none # quick\ncom.example.glued = none#quick\n", f);
    fputs("com.example.lastnone = 2\ncom.example.lastnone = none\n", f);
    fputs("com.example.lastnum = none\ncom.example.lastnum = 2\n", f);
    fputs("com.example.trgone = transient\ncom.example.trfirst = transient\ncom.example.trfirst = 2\n", f);
    fputs("com.example.neg = -1\ncom.example.prefix = none2\ncom.example.trail = 2abc\ncom.example.zero = 0\ncom.example.big = 11\ncom.example.empty =\n", f);
    fputs("com.example.badlater = 3\ncom.example.badlater = transient\n", f); // an invalid line leaves the earlier pin alone
    fputs("swicher = square\nnot a setting line\n", f); // a misspelt setting is a bad pin; a line without = is unknown
    fclose(f);
    setenv("HOME", dir, 1);
    config_load();
    unlink(path);
    snprintf(path, sizeof path, "%s/.config/just-desktops", dir);
    rmdir(path);
    snprintf(path, sizeof path, "%s/.config", dir);
    rmdir(path);
    rmdir(dir);
    if (config_modifier() != kCGEventFlagMaskControl) {
        printf("FAIL config_modifier() after a long no_preview line and \"modifier = ctrl\" = %llx; want control\n", (unsigned long long)config_modifier());
        failed++;
    }
    check_no_preview("com.example.long0", true);
    check_no_preview("com.example.long399", true);
    check_no_preview("com.example.after", true);
    check_no_preview("com.example.long400", false);
    check_no_preview("com.example.vault", true);
    check_no_preview("com.example.notes", true);
    check_no_preview("com.example.other", true);
    check_no_preview("com.example.pinned", false);
    check_no_preview("com.example", false);
    check_no_preview("com.1password.1password", true);
    if (config_assign("com.example.pinned") != 2) {
        printf("FAIL no_preview line disturbed pins\n");
        failed++;
    }

    check_pin("com.example.floating", CONFIG_EVERY);
    check_pin("com.example.caps", CONFIG_EVERY);
    check_pin("com.example.tight", CONFIG_EVERY);
    check_pin("com.example.note", CONFIG_EVERY);
    check_pin("com.example.glued", CONFIG_EVERY);
    check_pin("com.example.pinned", 2);
    check_pin("com.example.bogus", 0);
    check_pin("com.example.lastnone", CONFIG_EVERY);
    check_pin("com.example.lastnum", 2);
    check_pin("com.example.trail", 0);
    check_pin("com.example.zero", 0);
    check_pin("com.example.big", 0);
    check_pin("com.example.empty", 0);
    check_pin("com.example.badlater", 3);
    check_pin("swicher", 0);
    check_pin("com.example.neg", 0);
    check_pin("com.example.prefix", 0);
    check_pin("com.example.unknown", 0);
    check_pin("com.example.trgone", 0);
    check_pin("com.example.trfirst", 2);
    check_pin(NULL, 0);

    check_key_char(0, '1');
    check_key_char(2, '3');
    check_key_char(3, 'Q');
    check_key_char(5, 'E');
    const long long OPT = kCGEventFlagMaskAlternate, CMD = kCGEventFlagMaskCommand, CTRL = kCGEventFlagMaskControl;
    check_modifier("opt", OPT);
    check_modifier("option", OPT);
    check_modifier("alt", OPT);
    check_modifier("OPT", OPT);
    check_modifier("cmd", CMD);
    check_modifier("command", CMD);
    check_modifier("Command", CMD);
    check_modifier("ctrl", CTRL);
    check_modifier("control", CTRL);
    check_modifier("CTRL", CTRL);
    check_modifier("hyper", -1);
    check_modifier("garbage", -1);
    check_modifier("", -1);
    check_menubar("on", MB_STRIP);
    check_menubar("strip", MB_STRIP);
    check_menubar("Strip", MB_STRIP);
    check_menubar("icon", MB_ICON);
    check_menubar("Icon", MB_ICON);
    check_menubar("off", MB_OFF);
    check_menubar("OFF", MB_OFF);
    check_menubar("true", -1);
    check_menubar("yes", -1);
    check_menubar("garbage", -1);
    check_menubar("", -1);
    check_bool("on", 1);
    check_bool("ON", 1);
    check_bool("off", 0);
    check_bool("Off", 0);
    check_bool("true", -1);
    check_bool("yes", -1);
    check_bool("false", -1);
    check_bool("no", -1);
    check_bool("garbage", -1);
    check_bool("", -1);
    check_switcher("list", SW_LIST);
    check_switcher("square", SW_SQUARE);
    check_switcher("Square", SW_SQUARE);
    check_switcher("off", SW_OFF);
    check_switcher("on", SW_LIST);
    check_switcher("ON", SW_LIST);
    check_switcher("peek", -1);
    check_switcher("false", -1);
    check_switcher("no", -1);
    check_switcher("garbage", -1);
    check_switcher("", -1);
    if (config_switcher_style() != SW_LIST) {
        printf("FAIL default config_switcher_style() = %d; want %d\n", config_switcher_style(), SW_LIST);
        failed++;
    }
    check_layout();
    check("1 2 3 q w e", 6, (int[]){0x12, 0x13, 0x14, 0x0C, 0x0D, 0x0E}, 0);
    check("t y", 2, (int[]){0x11, 0x10}, 0);
    check("0 9", 2, (int[]){0x1D, 0x19}, 0);
    check("  a   j\tz ", 3, (int[]){0x00, 0x26, 0x06}, 0);
    check("Q W", 2, (int[]){0x0C, 0x0D}, 0);
    check("1 tab 2", 2, (int[]){0x12, 0x13}, 1);
    check("1 12 - 2", 2, (int[]){0x12, 0x13}, 2);
    check("1 1 2", 2, (int[]){0x12, 0x13}, 1);
    check("", 0, NULL, 0);
    check("tab esc", 0, NULL, 2);
    check("1 2 3 4 5 6 7 8 9 0 q w", 10, (int[]){0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1A, 0x1C, 0x19, 0x1D}, 2);
    // through a real file: inline comments, case, unknown values, last line wins
    load_text("border = off # note\nswitcher = square # note\nmenubar = icon # note\nmodifier = cmd # note\nkeys = a s # d f\nno_preview = com.x.one # com.x.two\n"
              "com.x.pin = 1 # note\ncom.x.pin = none\ncom.x.pin = 2 # last\n");
    if (config_border()) { printf("FAIL \"border = off # note\": config_border() = 1; want 0\n"); failed++; }
    if (config_switcher_style() != SW_SQUARE) { printf("FAIL \"switcher = square # note\": got %d; want %d\n", config_switcher_style(), SW_SQUARE); failed++; }
    if (config_menubar() != MB_ICON) { printf("FAIL \"menubar = icon # note\": got %d; want %d\n", config_menubar(), MB_ICON); failed++; }
    if (config_modifier() != kCGEventFlagMaskCommand) { printf("FAIL \"modifier = cmd # note\": got %llx; want command\n", (unsigned long long)config_modifier()); failed++; }
    if (config_workspaces() != 2 || config_key(0) != 0x00 || config_key(1) != 0x01) { printf("FAIL \"keys = a s # d f\": %d keys; want 2 (a s)\n", config_workspaces()); failed++; }
    check_no_preview("com.x.one", true);
    check_no_preview("com.x.two", false);
    check_no_preview("#", false);
    check_pin("com.x.pin", 2); // the last of its three lines
    load_text("border = ON\nswitcher = Square\nmenubar = ICON\nmodifier = Ctrl\nkeys = Q W E\ncom.x.caps = 3\n");
    if (!config_border()) { printf("FAIL \"border = ON\": config_border() = 0; want 1\n"); failed++; }
    if (config_switcher_style() != SW_SQUARE) { printf("FAIL \"switcher = Square\": got %d; want %d\n", config_switcher_style(), SW_SQUARE); failed++; }
    if (config_menubar() != MB_ICON) { printf("FAIL \"menubar = ICON\": got %d; want %d\n", config_menubar(), MB_ICON); failed++; }
    if (config_modifier() != kCGEventFlagMaskControl) { printf("FAIL \"modifier = Ctrl\": got %llx; want control\n", (unsigned long long)config_modifier()); failed++; }
    check_pin("com.x.pin", 0); // the previous load is gone
    check_pin("com.x.caps", 3);
    check_no_preview("com.x.one", false);
    load_text("border = off\nswitcher = square\nmenubar = icon\nmodifier = cmd\n"
              "border = garbage\nswitcher = peek\nmenubar = true\nmodifier = hyper\nkeys = tab esc\n");
    if (config_border()) { printf("FAIL \"border = garbage\" after \"border = off\": config_border() = 1; want 0\n"); failed++; }
    if (config_switcher_style() != SW_SQUARE) { printf("FAIL \"switcher = peek\" after square: got %d; want %d\n", config_switcher_style(), SW_SQUARE); failed++; }
    if (config_menubar() != MB_ICON) { printf("FAIL \"menubar = true\" after icon: got %d; want %d\n", config_menubar(), MB_ICON); failed++; }
    if (config_modifier() != kCGEventFlagMaskCommand) { printf("FAIL \"modifier = hyper\" after cmd: got %llx; want command\n", (unsigned long long)config_modifier()); failed++; }
    if (config_workspaces() != 6) { printf("FAIL \"keys = tab esc\": %d keys; want the default 6\n", config_workspaces()); failed++; }
    load_text("border = garbage\nswitcher = garbage\nmenubar = garbage\nmodifier = garbage\n");
    if (!config_border() || config_switcher_style() != SW_LIST || config_menubar() != MB_OFF || config_modifier() != kCGEventFlagMaskAlternate) {
        printf("FAIL unknown values: border %d switcher %d menubar %d modifier %llx; want the defaults 1 %d %d option\n", config_border(), config_switcher_style(), config_menubar(), (unsigned long long)config_modifier(), SW_LIST, MB_OFF);
        failed++;
    }
    // menubar default and the app's rule that there is always an item
    static const struct { bool app; const char *file; menubar_mode_t want; } mb[] = {
        {false, NULL, MB_OFF}, {false, "menubar = on\n", MB_STRIP}, {false, "menubar = icon\n", MB_ICON},
        {false, "menubar = off\n", MB_OFF}, {true, NULL, MB_STRIP}, {true, "menubar = off\n", MB_ICON},
        {true, "menubar = icon\n", MB_ICON}, {true, "menubar = strip\n", MB_STRIP},
    };
    for (size_t i = 0; i < sizeof mb / sizeof *mb; i++) {
        char home[] = "/tmp/jd_config_mb.XXXXXX", cmd[256];
        if (!mkdtemp(home)) return 1;
        snprintf(path, sizeof path, "%s/.config/just-desktops", home);
        snprintf(cmd, sizeof cmd, "mkdir -p '%s'", path);
        if (system(cmd)) return 1;
        if (mb[i].file) {
            snprintf(path, sizeof path, "%s/.config/just-desktops/config", home);
            f = fopen(path, "w");
            fputs(mb[i].file, f);
            fclose(f);
        }
        setenv("HOME", home, 1);
        config_set_app_mode(mb[i].app);
        config_load();
        if (config_menubar() != mb[i].want) {
            printf("FAIL app=%d menubar file %s: config_menubar() = %d; want %d\n", mb[i].app, mb[i].file ? mb[i].file : "(none)", config_menubar(), mb[i].want);
            failed++;
        }
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", home);
        if (system(cmd)) failed++;
    }
    puts(failed ? "FAILED" : "ok");
    return failed != 0;
}
