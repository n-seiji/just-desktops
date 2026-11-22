#pragma once
// The optional menu bar item: one glyph per window of the current desktop, or just the app glyph.
#include "config.h"

void menubar_init(menubar_mode_t mode);
bool menubar_ready(void); // the glyph strip is shown (menubar_init ran in strip mode)
// The current desktop ws and the n windows with a glyph (pids[i] owns wids[i]) in strip order; over
// more are only counted, over_hash identifies them; front is the focused window, 0 for none.
// Redraws only when one of them changed.
void menubar_update(int ws, const pid_t *pids, const uint32_t *wids, int n, int over, uint32_t over_hash, uint32_t front);
void menubar_flush(void); // send a strip just updated to WindowServer now, not after this pass of the run loop
void menubar_forget(pid_t pid); // drop an app's cached glyph
