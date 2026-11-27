# Journeys

Each one names how a user reaches the feature, how to drive it, and what proves it.

## Switch desktops with the hotkey

- **Reach.** Hold `⌥` and press a desktop key.
- **Drive.** `jd-drive key 18` for Desktop 1.
- **Proof.** The windows of that desktop are on screen in the recording. Switching away and back logs nothing: only a window whose desktop changes is logged.

## Pick a parked window in Mission Control

- **Reach.** Open Mission Control and click a window that belongs to another desktop.
- **Drive.** `jd-drive key 18`, wait 1.2 s, `open -a "Mission Control"`, wait 1 s, `jd-drive click <x> <y>` on the thumbnail.
- **Proof.** One `reason=follow trigger=ax-focus cur=1->N` line, and in the recording a single zoom that lands on the new desktop within about 6 frames at 16 fps, with no striped frames.
- **Gotchas.** Thumbnails move between runs, so take the point from a fresh frame. A click on a window of the current desktop logs nothing. The detection relies on WindowManager's layer 19 window, seen on macOS 27.2.1 only.

## Not driven yet

The `⌥Tab` switchers, `⌥[` and `⌥]`, moving a window with `⌥⇧` + key, the menu bar strip, and quit and restart. Add a section here when you drive one.
