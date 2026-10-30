# Workspaces

just-desktops keeps its own desktops. Windows of inactive desktops are parked in a corner of the main display, so macOS Spaces and SIP are never touched. This page covers history, pins, apps on every desktop, calling a window from another desktop, the border and the modifier.

## Desktops and keys

- The desktop keys are set with `keys` in the config: digits and letters, at most 10. The n-th key is Desktop n. Other keys pass through untouched.
- On exit, every parked window is moved back, and each window's desktop is saved for the next start: windows return to their desktops after a restart, or a quit followed by a launch within a minute. After a crash, parked windows stay in the bottom-right corner until the next launch, and they come back on the current desktop.
- Windows are parked on the main display only.

## History

`⌥[` goes back to the window you had focused before, and `⌥]` goes forward again, like a browser. It remembers the last 10 windows, across desktops. If the window is on another desktop, that desktop is switched to first.

- Closed and minimized windows are skipped and dropped from the history. Hidden windows of an app on every desktop are skipped too.
- A window of an [app on every desktop](#apps-on-every-desktop) is focused on the current desktop, without switching.
- Focusing a new window after going back drops the entries ahead. Closing a window makes macOS focus another one, and that one is recorded too if it is on the current desktop. A parked window it focuses is not.
- Windows you pass through with `⌥Tab` while `switcher = off` are not recorded. Only the one you end on is.
- The history is lost when just-desktops restarts.
- On US layouts `⌥[` and `⌥]` no longer type “ and ‘. `⌥⇧[` and `⌥⇧]` still type ” and ’.

## Calling a window from another desktop

A window stays on its desktop until you move it with `⌥⇧` and a number. Focusing a window that belongs to another desktop (Raycast, Cmd-Tab, Dock, a notification) switches to that desktop about 0.3 seconds later and leaves the window where it is. The delay lets just-desktops tell a call from macOS moving focus after you minimize, close or quit a window. A shortcut just before the window got focus (`⌘Tab`, an app's own shortcut, any key with `⌘`, `⌃` or `⌥`) switches at once, unless it was `⌘W`, `⌘Q`, `⌘M` or `⌘H`. Clicks, Return and those four shortcuts still wait the 0.3 seconds. Apps on every desktop are left out, since they belong to every desktop (see [Apps on every desktop](#apps-on-every-desktop)).

- A window summoned within 1.5 seconds of a switch is ignored, up to 3 seconds if macOS keeps activating apps. This is macOS handing focus around after the switch, not you. A click, Return, Enter or a shortcut (`⌘Tab`, an app's own shortcut, any key with `⌘`, `⌃` or `⌥`) just before the window got focus counts as you calling it: it is followed even in the quiet period. The same quiet period follows closing, minimizing, quitting and `⌥⇧` + number. `⌥⇧` + the current desktop's number on a window that is parked but focused brings it here.
- Closing, minimizing or quitting on a desktop never switches to another one, even if macOS focuses a window there next. After the quiet period (about 1.5 seconds, not instantly) a window of the current desktop gets focus back. A minimized window does not count: if the desktop has nothing else to focus, you stay on it.
- On a desktop with no window to focus, a parked window macOS focused after the quiet period is left alone until you switch. A window you minimized, closed or quit earlier does not make later calls look like that: calling another desktop's window from there switches to it.
- Hiding an app with `⌘H` is not covered: macOS may focus a window of another desktop, and that can still switch desktops.
- Pinned apps switch to their pin: with Safari pinned to 3, opening a link on desktop 1 takes you to desktop 3.
- If one app has windows on two desktops, you go to whichever window macOS picks.

## Pins

Pin an app to a desktop with `<bundle id> = <n>` in the config. Find a bundle id with `osascript -e 'id of app "Safari"'`.

```
com.apple.Safari = 1
com.googlecode.iterm2 = 2
```

- A window of a pinned app is placed on its desktop when it first appears, and it is not switched to. This covers windows that exist when just-desktops starts and windows opened later.
- A window that is focused when it appears, or that you focus later, switches to its desktop. Focusing a pinned app from another desktop takes you to its pin.
- The pin is applied once, when the window first appears.
- A pin to a desktop number higher than the number of `keys` is ignored.

## Apps on every desktop

Set `<bundle id> = none` in the config for an app you call up with a shortcut, such as a quick terminal or a password manager. The app belongs to every desktop, and it is hidden (Cmd-H) when you switch desktops, so it does not stay over the next one.

```
com.mitchellh.ghostty = none
```

- The app's windows are never parked.
- While the app is hidden it is not in ⌥Tab or the menu bar, and ⌥[ and ⌥] skip its windows. Otherwise they are listed on every desktop and get the border.
- Call it up with its own shortcut, the Dock or Cmd-Tab. macOS unhides it and it shows in place: nothing is parked, and no desktop switch happens.
- ⌥⇧ + key does nothing while one of them has focus.
- ⌥ + another desktop's key switches desktops and focuses that desktop's last window, if it has one. The current desktop's key does nothing, except while a `none` app is in front: it hides that app (other `none` apps stay) and focuses the desktop's last window. If the desktop is empty, macOS picks what gets focus and may pick a parked window in the corner; this is the same as on any empty desktop, after a switch or here.
- The setting is per app, so all of an app's windows are hidden, not only the quick one. A Finder set to `none` hides its windows at every switch; the desktop icons stay.
- To keep an app visible across switches, do not set it to `none`.
- Only regular apps with a tracked window are hidden. Whether an app's own shortcut shows it again after a hide depends on the app: panel-style quick windows may not come back, so try it.
- Hides are in the os_log with `reason=hide`.
- Changes apply on restart. A window of the app left in the corner after a crash is brought back at the next launch.
- For a password manager, also add it to `no_preview`: otherwise the square switcher screenshots it like any listed window.

## Border

The border is a thin ring floating 3pt off the focused window, brighter along the top.

- Its corners follow the window's own corner radius as WindowServer reports it (16pt for most windows, up to 26pt for newer styles). Without a reported radius it uses 16pt.
- It is white in Dark mode and black in Light mode. It follows the system appearance and updates the moment that changes. The switcher panel has a light and a dark look too.
- It is drawn through the private SkyLight framework, so no NSWindow is created.
- `border = off` creates no border window.

## Modifier

`⌥` in these docs is the base modifier. Option is the default. Set `modifier = cmd` or `modifier = ctrl` for Command or Control. `option`, `alt`, `command` and `control` are accepted too, in any case. An unknown value gives Option.

Every hotkey follows it: the desktop keys, moving a window, `⌥[` and `⌥]`, the switcher, releasing it to commit, Esc and the desktop keys while the panel is open. The Shift variants stay.

A key fires only when exactly the base modifier (plus Shift) is down. With `modifier = cmd`, holding Option or Control in addition lets the key through untouched. There is no Hyper key.

Command and Control collide with system shortcuts, and just-desktops swallows these keys system wide while it runs.

- With `cmd`, Cmd-Tab (the macOS app switcher) and Cmd-digit (app shortcuts such as browser tabs 1 to 9) stop working.
- With `cmd`, `⌘[` and `⌘]` (back and forward in browsers) stop working too.
- With `ctrl`, Ctrl-Tab and Ctrl-digit stop working, and so does `⌃[`, which is Esc in terminals. These are browser and terminal tab shortcuts, and Mission Control's desktop shortcuts if you enabled them.

just-desktops does not work around this. Choose a modifier whose combinations you do not need.
