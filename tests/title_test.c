#include "../src/title.h"
#include <stdio.h>
#include <string.h>

static int failed;

static void check(const char *title, const char *app, const char *want) {
    CFStringRef t = CFStringCreateWithCString(NULL, title, kCFStringEncodingUTF8);
    CFStringRef a = CFStringCreateWithCString(NULL, app, kCFStringEncodingUTF8);
    CFStringRef r = title_clean(t, a);
    char got[256] = "";
    if (r) CFStringGetCString(r, got, sizeof got, kCFStringEncodingUTF8);
    bool ok = want ? r && !strcmp(got, want) : !r;
    if (!ok) {
        printf("FAIL title_clean(\"%s\", \"%s\") = %s%s%s; want %s\n", title, app, r ? "\"" : "", r ? got : "NULL", r ? "\"" : "", want ? want : "NULL");
        failed++;
    }
    if (r) CFRelease(r);
    CFRelease(t);
    CFRelease(a);
}

static CFStringRef cf(const char *s) {
    return s ? CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8) : NULL;
}

static void check_labels(const char *app, const char *titles[], int n, const char *want[]) {
    CFStringRef a = cf(app), t[8], out[8];
    for (int i = 0; i < n; i++) t[i] = cf(titles[i]);
    title_labels(a, t, n, out);
    for (int i = 0; i < n; i++) {
        char got[256];
        CFStringGetCString(out[i], got, sizeof got, kCFStringEncodingUTF8);
        if (strcmp(got, want[i])) {
            printf("FAIL title_labels(\"%s\", n=%d)[%d] = \"%s\"; want \"%s\"\n", app, n, i, got, want[i]);
            failed++;
        }
        CFRelease(out[i]);
        if (t[i]) CFRelease(t[i]);
    }
    CFRelease(a);
}

// n windows given as {app, title} pairs; want is each row text, in order.
static void check_all(const char *name, const char *pairs[][2], int n, const char *want[]) {
    CFStringRef apps[8], titles[8];
    for (int i = 0; i < n; i++) {
        apps[i] = cf(pairs[i][0]);
        titles[i] = cf(pairs[i][1]);
    }
    title_label_all(apps, titles, n);
    for (int i = 0; i < n; i++) {
        char got[256] = "";
        if (titles[i]) CFStringGetCString(titles[i], got, sizeof got, kCFStringEncodingUTF8);
        bool ok = want[i] ? titles[i] && !strcmp(got, want[i]) : !titles[i];
        if (!ok) {
            printf("FAIL title_label_all %s[%d] = %s%s%s; want %s\n", name, i, titles[i] ? "\"" : "", titles[i] ? got : "NULL", titles[i] ? "\"" : "", want[i] ? want[i] : "NULL");
            failed++;
        }
        if (titles[i]) CFRelease(titles[i]);
        if (apps[i]) CFRelease(apps[i]);
    }
}

int main(void) {
    check("side-project ⌥Tab ウィンドウ一覧の案 - Google Chrome - Pat", "Google Chrome", "side-project ⌥Tab ウィンドウ一覧の案");
    check("team-ops（チャンネル） - acme - Slack", "Slack", "team-ops（チャンネル） - acme");
    check("Claude", "Claude", NULL);
    check("Release Notes: 1.142.0 — example", "Code", "Release Notes: 1.142.0 — example");
    check("Pats-MacBook-Air.local: ------", "Ghostty", "Pats-MacBook-Air.local: ------");
    check("Inbox – Mail – Gmail", "gmail", "Inbox – Mail");
    check("Docs — Notes — Notes", "Notes", "Docs");
    check("Slack - Slack", "Slack", NULL);
    check("  padded  - Slack", "Slack", "padded");
    check("", "Slack", NULL);
    check("  ", "Slack", NULL);
    check_labels("Slack", (const char *[]){"general"}, 1, (const char *[]){"Slack"});
    check_labels("Google Chrome", (const char *[]){"Docs", "Inbox"}, 2, (const char *[]){"Docs", "Inbox"});
    check_labels("Code", (const char *[]){NULL, NULL}, 2, (const char *[]){"Code 1", "Code 2"});
    check_labels("Code", (const char *[]){"main.c", NULL, "main.c"}, 3, (const char *[]){"main.c", "Code 2", "main.c 3"});
    check_all("single", (const char *[][2]){{"Slack", "general"}}, 1, (const char *[]){"Slack"});
    check_all("two of one app", (const char *[][2]){{"Chrome", "Docs"}, {"Chrome", "Inbox"}}, 2, (const char *[]){"Docs", "Inbox"});
    // groups number from 1 on their own, and rows keep the input order
    check_all("interleaved", (const char *[][2]){{"Code", NULL}, {"Slack", "general"}, {"Code", NULL}, {"Mail", NULL}}, 4,
              (const char *[]){"Code 1", "Slack", "Code 2", "Mail"});
    check_all("same title", (const char *[][2]){{"Code", "main.c"}, {"Mail", "main.c"}, {"Code", "main.c"}}, 3,
              (const char *[]){"main.c", "Mail", "main.c 2"});
    // the app name is compared exactly: a differently cased app is its own group
    check_all("case", (const char *[][2]){{"Code", NULL}, {"code", NULL}}, 2, (const char *[]){"Code", "code"});
    // an empty app name is a name like any other
    check_all("empty app", (const char *[][2]){{"", NULL}, {"", "x"}}, 2, (const char *[]){" 1", "x"});
    // a NULL app is skipped and its title kept, NULL or not
    check_all("no app", (const char *[][2]){{NULL, "t"}, {NULL, NULL}, {"Code", NULL}}, 3, (const char *[]){"t", NULL, "Code"});
    check_all("none", NULL, 0, NULL);
    puts(failed ? "FAILED" : "ok");
    return failed != 0;
}
