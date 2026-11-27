# Journeys

Each one names how a user reaches the feature, how to drive it, and what proves it.

## Switch desktops with the hotkey

- **Reach.** Hold `⌥` and press a desktop key.
- **Drive.** `jd-drive key 18` for Desktop 1.
- **Proof.** The windows of that desktop are on screen in the recording. Switching away and back logs nothing: only a window whose desktop changes is logged.

## Pick a parked window in Mission Control

- **Reach.** Open Mission Control and click a window that belongs to another desktop.
- **Drive.** `jd-drive key 18`, wait 1.2 s, `open -a "Mission Control"`, wait 1 s, `jd-drive click <x> <y>` on the thumbnail.
- **Proof.** One `reason=follow trigger=ax-focus cur=1->N` line and the new desktop on screen in the recording.
- **Gotchas.**
  - A posted click is not a hardware click to just-desktops: `CGEventSourceSecondsSinceLastEventType` does not count it (measured with `CGEventPost` and `IOHIDPostEvent`). So this journey takes the 0.3 s path: a zoom to the old desktop, then the switch. The at-once path, one zoom that lands on the new desktop with no striped frames, needs a real click. Ask the user to click, and record while they do.
  - For the same reason a posted click is never "you calling a window" during a quiet period. Wait 4 s after a switch before the click.
  - Thumbnails move between runs, so take the point from a fresh frame. A click on a window of the current desktop logs nothing.
  - Mission Control is recognised by WindowManager's display-sized layer 19 window, seen on macOS 27.2.1 only.

## Switch desktops with the hotkey while Mission Control is open

- **Reach.** Open Mission Control, then hold `⌥` and press a desktop key.
- **Drive.** `open -a "Mission Control"`, wait 1 s, `jd-drive key 19`.
- **Proof.** No torn frame in the recording, and after Mission Control closes the other desktop's windows are on screen. Mission Control keeps showing the thumbnails it opened with.
- **Gotchas.** `jd-drive mc` is also 1 in App Exposé (`open -a "Mission Control" --args 2`), which uses the same shield window, and 0 in Show Desktop (`--args 1`).

## Not driven yet

The `⌥Tab` switchers, `⌥[` and `⌥]`, moving a window with `⌥⇧` + key, the menu bar strip, and quit and restart. Add a section here when you drive one.
