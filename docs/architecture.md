# Architecture

just-desktops is one process on the main run loop. It emulates desktops by moving windows, so it never touches macOS Spaces. This page is the map for reading the code in `src/`.

## Modules

| File | Job |
| --- | --- |
| `main.m` | Start up, single-instance lock, signals, `jd_quit` and `jd_restart`, the way out (save desktops, restore windows). |
| `app.m` | The `.app` bundle only: first-run Accessibility panel, login item, About, settings writes. |
| `config.c`, `config_write.c` | Read the config file once; write one key back for the app's settings menu. |
| `apps.m` | NSWorkspace launch, quit, activate and hide events; app names and icons; hiding apps. |
| `wm.c` | The apps and windows we track, and the Accessibility notifications that update them. |
| `wm_focus.c` | Park and unpark windows, switch desktops, move a window, follow a parked window that gets focus, the `ws` log. |
| `wm_list.c` | The window lists the switchers and the menu bar draw from. |
| `ax.c` | Typed reads of Accessibility attributes. |
| `hotkey.c` | The keyboard event tap: desktop keys, ⌥[ ⌥], ⌥Tab, Esc. |
| `switcher.c`, `switcher_card.c`, `swkit.c` | List and instant switcher, square switcher, the SkyLight drawing helpers they share. |
| `thumb.m` | Window screenshots for the square switcher (ScreenCaptureKit). |
| `border.c` | The focus ring: four thin SkyLight windows. |
| `appearance.c` | Whether the system is in Dark mode; the ring and the switchers pick their colours by it. |
| `menubar.m` | The status item: strip drawing, click handling. |
| `menubar_glyph.m`, `menubar_overflow.m`, `menubar_menu.m` | App glyph masks, the "+k" panel, the right-click menu. |

Pure logic, with a unit test each in `tests/`: `select.c` (selection stepping), `keyrules.c` (modifier match, repeat throttle), `strip.c` (strip geometry and hit test), `order.c` (menu bar order and overflow split), `membership.c` (the saved desktops file: format, parse, pins), `shown.c` (visibility, grace period, parking point), `follow.c` (what to do when a window gets focus), `hist.c` (focus history for ⌥[ ⌥]), `cardmath.c` (easing, card layout and the list highlight's slide), `border_geom.c` (ring rects), `glyph.c` (icon to mask), `title.c` (row text).

Headers: one per module, and `common.h` for the limits and `sw_entry_t` several modules share. `wm_priv.h` and `menubar_priv.h` are for the files of that module only.

## Data flow

Events come in from three places: Accessibility observers (window created, destroyed, focused, minimized), NSWorkspace (app launched, quit, activated) and the keyboard tap. `wm*.c` is the only owner of state: which windows exist, their desktop, whether they are parked, which desktop is current.

After every change `wm_refresh_ui` reads the focused window once, then calls `border_show` or `border_hide` and `menubar_update`. The switchers pull a fresh list from `wm_windows` when they open and call `wm_focus_wid` to commit. Nothing outside `wm*.c` writes window state.

## Leaving and restarting

Every way out calls one function in `main.m`, and only the first call does anything: `jd_quit` (menu, signals), `jd_restart` (settings change), `NSApplicationWillTerminateNotification` (the Quit Apple Event that `brew upgrade`, logout and Activity Monitor send to the app), and the keyboard tap failing at start. There is no `atexit` handler, because a second instance that exits on the lock must not run it.

It first saves the current desktop and each window's pid, window id and desktop to `just-desktops.state` in the per-user temp directory (the lock's directory), written to a temporary file and renamed, with a timestamp. Then it puts parked windows back on screen. That walk stops after 2.5 seconds, skips apps that are terminating and gives each Accessibility call 0.25 seconds, so a hung app cannot hold up a logout.

At start, before `apps_init`, the file is read and deleted. A file older than 60 seconds, or more than 5 seconds ahead of the clock, is ignored. A window whose pid and window id match gets its saved desktop, unless its app is pinned, which wins; desktops above `desktops` are clamped. The table stays in memory for 30 seconds for windows that are tracked late, each entry used once. Windows that match nothing go to the current desktop, which is also restored. `membership.c` holds the format and parser, so the tests cover them.

## Diagnostics

Each time a window's desktop is set, `wm_log_ws` writes one `os_log` line (subsystem `dev.n-seiji.just-desktops`, category `ws`). Read it with:

```
log show --last 1h --predicate 'subsystem == "dev.n-seiji.just-desktops"'
```

A desktop change looks like `wid=412 app=Google Chrome ws=3->1 reason=move trigger=hotkey cur=3 settling=0 stale=0 same_app_here=0 idle_ms=820`. A parked window that got focus looks like `wid=412 app=Google Chrome ws=3 reason=follow trigger=ax-focus cur=1->3 settling=0 stale=0 same_app_here=0 idle_ms=820` (`refocus`, `left` and `dropped` print `cur=1`: the desktop does not change).

- `ws=old->new`: on a desktop change. `-1` as the old value is a window just tracked, `0` is on every desktop. A `follow` or `refocus` line has a single `ws`, the window's own desktop.
- `reason`: `follow` (a parked window was focused and still was 0.3 s later, so the desktop switched; `cur` is old->new), `refocus` (a parked window kept focus after a switch, close, minimize, quit or `left` settled; a window of this desktop was focused), `left` (a parked window was focused 0.3 s ago, but the window last used on this desktop was minimized, closed or its app quit: not a call, so no switch), `dropped` (a parked window was focused, but a close, minimize or quit noticed within the 0.3 s started a quiet period: no switch, a recheck follows), `move` (the move key), `track` (a new window), `restore-file` (a new window that got its desktop from the saved file).
- `trigger`: the last thing that led to a focus check: `ax-focus`, `ax-created`, `ax-destroyed`, `activated`, `terminated`, `recheck` (the one after a desktop switch, close, minimize, quit or `left` settles), `hotkey`, `menubar`, `retry` (an app that was not ready for Accessibility yet) or `start`. It is the last one seen, not proof of cause.
- `settling` and `stale`: a quiet period was running (after a switch, close, minimize or quit); the window is the one just parked or left alone.
- `same_app_here`: other windows of the same app on the current desktop, not counting this one.
- `idle_ms`: time since the last keyboard or mouse event. A few seconds or more on a `follow` suggests macOS activated the app on its own, not you.

Window tracking also logs `found parked by a previous run` when it finds a window left in the corner. A window of the current desktop is moved to the top left of the main display; a window of another desktop stays parked there. App names and reasons are logged as public; window titles are never logged.

## Threading

There is one thread. The event tap, the Accessibility observers, the SkyLight notification port, the animation timers and the signal handlers all run on the main run loop. The tap callback only decides and then hops to the main queue with `dispatch_async`; the work happens there. With the menu bar on, `[NSApp run]` is that loop; without it, a plain `CFRunLoopRunInMode` loop with an autorelease pool per event. ScreenCaptureKit completions are delivered on the main queue.

## Memory rules

Idle footprint is the product, so these are rules, not tuning.

- No AppKit unless the menu bar is on. The border, both panel switchers and all hotkeys use SkyLight and CoreGraphics directly.
- Allocate on demand, free on close. A switcher's windows, contexts, fonts, icons and entries exist only while ⌥ is held. Closing calls `malloc_zone_pressure_relief`.
- Draw large surfaces once. Flushing a SkyLight context again keeps extra backing surfaces alive, so the list highlight and the square cards are separate windows that move, and the cards are drawn at 1x.
- Thin windows over big ones. The ring is four thin windows because a window's backing store is its full rect.
- Timers run only while something moves.
- No NSPopover (on macOS 26 each one keeps a 5MB surface after closing); the "+k" panel is one hand-drawn view.
- ScreenCaptureKit is linked weakly and touched only for the square style.
- Glyph masks are cached per app and dropped when the app quits.

## Private APIs

All declarations are in `skylight.h`. SkyLight (window creation, tags, shapes, ordering, level, blur, move and resize notifications, corner radii) is used by `border.c`, `swkit.c`, `switcher.c` and `switcher_card.c`. The one HIServices call, `_AXUIElementGetWindow` (Accessibility element to window id), is used by `wm.c`. A macOS update can break these; everything else is public API.
