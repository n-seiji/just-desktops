#pragma once
// What NSWorkspace knows about running apps.
#include <ApplicationServices/ApplicationServices.h>

void apps_init(void); // starts the launch / quit / activate / hide / unhide events and adds the running apps to wm
pid_t apps_frontmost(void);
CGImageRef apps_icon(pid_t pid, int px); // +1, a px x px copy of the app's icon, or NULL
CFStringRef apps_name(pid_t pid);        // +1 localized name, or NULL
bool apps_alive(pid_t pid);              // running and not terminating
bool apps_hide(pid_t pid);               // hide the app (Cmd-H); skips one already hidden or terminated; true when it asked
bool apps_hidden(pid_t pid);
