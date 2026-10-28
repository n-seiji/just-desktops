#pragma once
#include <stdbool.h>

bool hotkey_init(void); // false when the keyboard event tap cannot be created (no permission)

// When Return, keypad Enter or Cmd-Tab was last pressed (CFAbsoluteTime, 0 if never): keys that call a window up.
double hotkey_last_call_key(void);
