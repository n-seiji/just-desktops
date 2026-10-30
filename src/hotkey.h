#pragma once
#include <stdbool.h>

bool hotkey_init(void); // false when the keyboard event tap cannot be created (no permission)

// When Return, keypad Enter or a key with Command, Control or Option was last pressed (CFAbsoluteTime, 0 if
// never): keys that call a window up, such as Cmd-Tab or an app's own shortcut.
double hotkey_last_call_key(void);
// CFAbsoluteTime of the last non-autorepeat keyDown if it was a shortcut that is not close, quit, minimize or hide, else 0.
double hotkey_last_quick_key(void);
