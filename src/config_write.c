#include "config_write.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Every setting commented out at its default, so a freshly created file changes nothing.
static const char sample[] =
    "# just-desktops config. Remove the leading # of a line to set it; restart to apply.\n"
    "# The menu bar item rewrites only the line it changes.\n"
    "\n"
    "# Base modifier of every hotkey: opt | cmd | ctrl\n"
    "# modifier = opt\n"
    "\n"
    "# Border around the focused window: on | off\n"
    "# border = on\n"
    "\n"
    "# Option-Tab window switcher: list | square | off (off switches at once, no panel)\n"
    "# switcher = list\n"
    "\n"
    "# Menu bar item: on (a glyph per window) | icon (only the app glyph) | off (command line only).\n"
    "# The app always shows an item, and treats off as icon.\n"
    "# menubar = on\n"
    "\n"
    "# Keys that switch desktops. The n-th key is Desktop n. Digits and letters, at most 10.\n"
    "# keys = 1 2 3 q w e\n"
    "\n"
    "# Apps the square switcher never screenshots, as bundle ids (password managers are built in).\n"
    "# no_preview = com.example.vault\n"
    "\n"
    "# Pin an app to a desktop: <bundle id> = <desktop number>\n"
    "# Find a bundle id with: osascript -e 'id of app \"Safari\"'\n"
    "# com.apple.Safari = 1\n"
    "\n"
    "# Keep an app on every desktop, hidden (Cmd-H) when you switch desktops: <bundle id> = none\n"
    "# For apps you call up with a shortcut, such as a quick terminal or a password manager.\n"
    "# For a password manager, also add it to no_preview so the square switcher never screenshots it.\n"
    "# com.mitchellh.ghostty = none\n";

const char *config_sample(void) { return sample; }

bool config_path(char *buf, size_t n) {
    const char *home = getenv("HOME");
    return (size_t)snprintf(buf, n, "%s/.config/just-desktops/config", home ? home : "") < n;
}

// True when line [s, e) is an assignment of key (not a comment).
static bool is_assignment(const char *s, const char *e, const char *key) {
    while (s < e && (*s == ' ' || *s == '\t')) s++;
    if (s == e || *s == '#') return false;
    const char *eq = memchr(s, '=', (size_t)(e - s));
    if (!eq) return false;
    while (eq > s && (eq[-1] == ' ' || eq[-1] == '\t')) eq--;
    return (size_t)(eq - s) == strlen(key) && !memcmp(s, key, strlen(key));
}

char *config_set(const char *text, const char *key, const char *value) {
    size_t len = strlen(text);
    const char *hit = NULL, *hit_end = NULL; // the last assignment wins when the file is read
    for (const char *s = text; s < text + len;) {
        const char *nl = memchr(s, '\n', (size_t)(text + len - s));
        const char *e = nl ? nl : text + len;
        const char *content_end = e > s && e[-1] == '\r' ? e - 1 : e; // keep a CR where there is one
        if (is_assignment(s, content_end, key)) {
            hit = s;
            hit_end = content_end;
        }
        s = nl ? nl + 1 : text + len;
    }

    size_t cap = len + strlen(key) + strlen(value) + 8;
    char *out = malloc(cap);
    if (!out) return NULL;
    if (hit) {
        int head = (int)(hit - text);
        snprintf(out, cap, "%.*s%s = %s%s", head, text, key, value, hit_end);
    } else {
        snprintf(out, cap, "%s%s%s = %s\n", text, len && text[len - 1] != '\n' ? "\n" : "", key, value);
    }
    return out;
}

static int mkdirs(const char *path) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", path);
    for (char *p = dir + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        int r = mkdir(dir, 0755);
        *p = '/';
        if (r != 0 && errno != EEXIST) return -1;
    }
    return 0;
}

// Writes data to a temporary file next to path, then renames it over path. A symlink at path
// (dotfiles managers make them) is followed, so the link itself survives.
static int write_atomic(const char *path, const char *data) {
    char real[PATH_MAX], tmp[PATH_MAX + 32];
    if (!realpath(path, real)) snprintf(real, sizeof real, "%s", path);
    snprintf(tmp, sizeof tmp, "%s.tmp.%d", real, (int)getpid());
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    struct stat st;
    if (stat(real, &st) == 0) fchmod(fileno(f), st.st_mode & 0777);
    bool ok = fputs(data, f) >= 0;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, real) != 0) {
        int e = errno;
        unlink(tmp);
        errno = e;
        return -1;
    }
    return 0;
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return errno == ENOENT ? strdup("") : NULL;
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    while (buf) {
        n += fread(buf + n, 1, cap - n - 1, f);
        if (n < cap - 1) break;
        char *g = realloc(buf, cap *= 2);
        if (!g) { free(buf); buf = NULL; }
        else buf = g;
    }
    bool bad = ferror(f);
    fclose(f);
    if (!buf || bad) { free(buf); return NULL; }
    buf[n] = 0;
    return buf;
}

int config_write_key(const char *path, const char *key, const char *value) {
    if (mkdirs(path) != 0) return -1;
    char *old = slurp(path);
    if (!old) return -1;
    char *text = config_set(old, key, value);
    free(old);
    if (!text) { errno = ENOMEM; return -1; }
    int r = write_atomic(path, text);
    free(text);
    return r;
}

int config_ensure_file(const char *path) {
    if (access(path, F_OK) == 0) return 0;
    if (mkdirs(path) != 0) return -1;
    return write_atomic(path, sample);
}
