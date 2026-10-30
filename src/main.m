#import <AppKit/AppKit.h>
#include "app.h"
#include "apps.h"
#include "border.h"
#include "config.h"
#include "hotkey.h"
#include "menubar.h"
#include "thumb.h"
#include "wm.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;
static dispatch_source_t signal_sources[3];

// The app is identified by its bundle id. Any value of JD_APP_MODE turns the app mode on for the bare binary:
// a test hook for the app's menu and settings without building a bundle; it is not a user setting.
bool jd_app_mode(void) {
    static int app = -1;
    if (app < 0) {
        CFBundleRef b = CFBundleGetMainBundle();
        CFStringRef id = b ? CFBundleGetIdentifier(b) : NULL;
        app = (id && CFEqual(id, CFSTR("dev.n-seiji.just-desktops"))) || getenv("JD_APP_MODE") != NULL;
    }
    return app;
}

// A file in the per-user temp directory (mode 0700, unlike /tmp).
static bool temp_path(const char *name, char *path, size_t cap) {
    char dir[PATH_MAX];
    size_t len = confstr(_CS_DARWIN_USER_TEMP_DIR, dir, sizeof dir);
    if (len < 2 || len > sizeof dir) return false;
    int n = snprintf(path, cap, "%s%s%s", dir, dir[len - 2] == '/' ? "" : "/", name);
    return n > 0 && (size_t)n < cap;
}

#define STATE_FILE "just-desktops.state"
static bool managing; // wm_init ran: there is a state worth saving

// Reads the desktops the previous run saved, then deletes the file so a later crash cannot replay it.
static void load_membership(void) {
    char path[PATH_MAX];
    if (!temp_path(STATE_FILE, path, sizeof path)) return;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return;
    struct stat st;
    char *text = NULL;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() && st.st_size < (1 << 20) && (text = malloc(st.st_size + 1))) {
        ssize_t n = read(fd, text, st.st_size);
        text[n > 0 ? n : 0] = 0;
    }
    close(fd);
    unlink(path);
    if (text) wm_restore_membership(text);
    free(text);
}

static void save_membership(void) {
    char path[PATH_MAX], tmp[PATH_MAX];
    char *text = wm_membership();
    if (text && temp_path(STATE_FILE, path, sizeof path) && snprintf(tmp, sizeof tmp, "%s.%d", path, getpid()) < (int)sizeof tmp) {
        unlink(tmp);
        int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0) {
            size_t len = strlen(text);
            bool ok = write(fd, text, len) == (ssize_t)len;
            if (close(fd) != 0 || !ok || rename(tmp, path) != 0) unlink(tmp);
        }
    }
    free(text);
}

// Every way out goes through here, and only the first caller does the work: the state is saved (needs no
// Accessibility, so it comes first), then the windows come back. There is no atexit handler: a second
// instance that exits on the lock must not run this and overwrite the file with an empty one.
static void leave(void) {
    static bool done;
    if (done || !managing) return;
    done = true;
    save_membership();
    wm_restore_all();
    border_hide();
}

void jd_quit(void) {
    leave();
    exit(0);
}

// Windows come back on screen, then a helper starts this program again once this process is gone
// and has released the lock, and this process exits. (exec in place is not an option: the new
// image cannot reach LaunchServices, so it sees no running apps and draws an empty strip.) A bundle
// is opened through LaunchServices, a bare binary is run directly.
void jd_restart(void) {
    leave();
    char path[PATH_MAX], pid[16];
    uint32_t n = sizeof path;
    if (_NSGetExecutablePath(path, &n) == 0) {
        char *bundle = strstr(path, ".app/Contents/MacOS/");
        if (bundle) bundle[4] = 0; // path is now the .app
        snprintf(pid, sizeof pid, "%d", getpid());
        char *args[] = {"sh", "-c",
                        "while kill -0 \"$1\" 2>/dev/null; do sleep 0.1; done; "
                        "if [ -d \"$2\" ]; then exec /usr/bin/open \"$2\"; else exec \"$2\"; fi",
                        "sh", pid, path, NULL};
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
        int err = posix_spawn(NULL, "/bin/sh", NULL, &attr, args, environ);
        if (err == 0) exit(0);
        errno = err;
    }
    fprintf(stderr, "just-desktops: restart failed: %s\n", strerror(errno));
    exit(1);
}

// Takes the single-instance lock in the per-user temp directory. Exits 0 when
// another instance holds it: launchd's KeepAlive would respawn a nonzero exit forever. Any other
// failure is a real error and exits 1. The descriptor stays open for the life of the process.
static void acquire_lock(void) {
    char path[PATH_MAX];
    if (!temp_path("just-desktops.lock", path, sizeof path)) {
        fprintf(stderr, "just-desktops: cannot find the per-user temp directory: %s\n", strerror(errno));
        exit(1);
    }
    // close-on-exec: the restart helper must not inherit the lock, or the new instance could not take it
    int fd = open(path, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid()) {
        fprintf(stderr, "just-desktops: cannot use the lock file %s: %s\n", path,
                fd < 0 ? strerror(errno) : "not a regular file owned by you");
        exit(1);
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno != EWOULDBLOCK) {
            fprintf(stderr, "just-desktops: cannot lock %s: %s\n", path, strerror(errno));
            exit(1);
        }
        fprintf(stderr, "just-desktops: already running\n");
        exit(0);
    }
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-v")) {
            puts(JD_VERSION);
            return 0;
        }
        fprintf(stderr, "usage: just-desktops [--version]\n");
        return 1;
    }

    acquire_lock();
    @autoreleasepool {
        // Installed before the Accessibility wait so a signal there exits cleanly too.
        int sigs[3] = {SIGINT, SIGTERM, SIGHUP};
        for (int i = 0; i < 3; i++) {
            signal(sigs[i], SIG_IGN);
            signal_sources[i] = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, sigs[i], 0, dispatch_get_main_queue());
            dispatch_source_set_event_handler(signal_sources[i], ^{ jd_quit(); });
            dispatch_resume(signal_sources[i]);
        }

        NSDictionary *opts = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @YES};
        if (!AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts)) {
            fprintf(stderr, "just-desktops: waiting for Accessibility permission\n");
            if (jd_app_mode()) {
                app_wait_accessibility();
            } else {
                // Poll on the run loop, not sleep(): signals keep working while we wait.
                while (!AXIsProcessTrusted()) {
                    @autoreleasepool {
                        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1, false);
                    }
                }
            }
        }
        config_set_app_mode(jd_app_mode());
        config_load();
        if (config_switcher_style() == SW_SQUARE) thumb_request_access();
        wm_init();
        load_membership();
        managing = true;
        [NSNotificationCenter.defaultCenter addObserverForName:NSApplicationWillTerminateNotification object:nil queue:nil
                                                    usingBlock:^(NSNotification *n) { (void)n; leave(); }];
        apps_init();
        if (config_menubar() != MB_OFF) menubar_init(config_menubar());
        // After AppKit is up: edge windows made before it stay ordered out, except the first.
        if (config_border()) border_init();
        if (!hotkey_init()) {
            fprintf(stderr, "just-desktops: could not create keyboard event tap\n");
            leave();
            return 1;
        }

        wm_set_trigger("start");
        wm_settle(); // a parked or pinned window focused at launch is not a call for its desktop
        wm_on_focus();
    }

    // The status item needs AppKit's own event loop to draw and take clicks.
    if (config_menubar() != MB_OFF) [NSApp run];

    // Drain autoreleased objects after every event so memory stays flat.
    for (;;) {
        @autoreleasepool {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 3600, true);
        }
    }
}
