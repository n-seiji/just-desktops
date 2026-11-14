#pragma once
// The process lifecycle (main.m) and the .app bundle (app.m).
#include <stdbool.h>

bool jd_app_mode(void); // running as dev.n-seiji.just-desktops.app
void jd_quit(void);     // put every parked window back and exit(0): the one clean way out
void jd_restart(void);  // put every parked window back and start this executable again

void app_wait_accessibility(void); // first-run panel; returns once Accessibility is granted
void app_set(const char *key, const char *value); // write one config key, then jd_restart
void app_open_config(void);
typedef enum { LOGIN_OFF, LOGIN_ON, LOGIN_NEEDS_APPROVAL } login_state_t; // the last: waiting for approval in System Settings
login_state_t app_login_state(void);
void app_toggle_login(void);
void app_open_about(void);
