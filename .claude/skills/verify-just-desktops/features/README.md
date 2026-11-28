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
- **Gotchas.** `jd-drive mc` is also 1 in App Exposé (`open -a "Mission Control" --args 2`), which uses the same shield window, and 0 in Show Desktop (`--args 1`). It stays 0 with Stage Manager on (`defaults write com.apple.WindowManager GloballyEnabled -bool true`, read back and restored after).

## Status of the journeys below

A journey with a **Seen** line was run, on macOS 27.2 with `switcher = off` and `menubar = on` unless it says otherwise. The others are scripted, not run: run one and write what you saw under it, or correct the script. Key codes: `1` 18, `2` 19, `3` 20, `q` 12, Tab 48, `[` 33, `]` 30, Esc 53. Set `switcher` and the other settings in `~/.config/just-desktops/config`, restart the app to apply them, and put the user's config back after.

## Switch windows with ⌥Tab, quick tap

- **Reach.** Tap `⌥Tab` and release at once, with `switcher = list`, `square` and `off`.
- **Drive.** Put two windows of one desktop on screen. `jd-drive chord opt 48`, then `jd-drive chord optshift 48` for the other direction.
- **Proof.** The focused window changes with no panel: `jd-drive windows` right after lists no just-desktops window. The release is 30 ms after the Tab, inside the 150 ms before a panel is drawn, so this is the instant commit. With `off` the focus changes on the Tab itself. Log lines appear only when a window changes desktop.

## Switch windows with ⌥Tab, held

- **Reach.** Hold `⌥`, press Tab, wait, release.
- **Drive.** `jd-drive chord opt 48 -n 3 -t 400`, with `switcher = list`, then `square`. Poll `jd-drive windows` during the 400 ms, or record 4 s and read the sheet. Then `jd-drive chord opt 48 -n 2 -t 400 -a`, and `jd-drive chord optshift 48 -n 2 -t 400`.
- **Proof.** A panel of the chosen style is on screen before the release and gone after it. The window after three steps is the one focused. A held Tab repeats only every 120 ms, so the `-a` run steps fewer times than it posted. With `off` there is no panel and each step focuses the next window at once.
- **Seen.** `jd-drive chord opt 48 -n 2 -t 1200` with `switcher = list`: a screenshot 0.9 s in shows the panel with the second row highlighted. `jd-drive windows` listed no just-desktops window while it was open, so use a screenshot or `windows all`. `square`, `-a` and `optshift` held are not run.
- **Gotchas.** `square` shows screenshots only with Screen Recording granted, and the app icon otherwise.

## Cancel a switcher with Esc

- **Reach.** Hold `⌥`, press Tab, press Esc, release `⌥`.
- **Drive.** Run `jd-drive probe` for the swallow rule. For the effect, record while pressing the keys by hand: `chord` cannot send Esc with the modifier held.
- **Proof.** The panel closes and the focus stays on the first window. With `switcher = off` there is no panel and the focus has already moved on the Tab. In the probe table, `esc-armed` is `swallow` and `esc-not-armed` is `pass`.

## Go back and forward with ⌥[ and ⌥]

- **Reach.** Focus window A, then B on another desktop, then C.
- **Drive.** `jd-drive key 18`, focus a window, `jd-drive key 19`, focus another, then `jd-drive key 33`, wait 1.2 s, `jd-drive key 33`, wait 1.2 s, `jd-drive key 30`.
- **Proof.** The previous window is on screen after each press: compare `jd-drive windows all` before and after. No window changes desktop, so nothing is logged.
- **Seen.** `jd-drive chord opt 33` then `chord opt 30` after three desktop switches: no log line, and the window positions were the same in three runs.

## Move a window with ⌥⇧ and a desktop key

- **Reach.** Focus a window, hold `⌥⇧` and press a desktop key.
- **Drive.** `jd-drive chord optshift 19` moves the focused window to Desktop 2. `jd-drive key 19` goes there, `jd-drive key 18` goes back.
- **Proof.** One `reason=move trigger=hotkey ws=1->2` line. The window is gone from the screen on Desktop 1 and on screen on Desktop 2. Moving to the current desktop logs nothing.
- **Seen.** `chord optshift 19`, `key 19`, `chord optshift 18`, `key 18`: `ws=1->2 reason=move trigger=hotkey cur=1 settling=0` then `ws=2->1 reason=move trigger=hotkey cur=2 settling=1`, the same in three runs. In `windows all` the moved window sat at the corner of the right display until its desktop was shown.

## Keys that must not be caught

- **Reach.** Press the combinations the tap lets through.
- **Drive.** `jd-drive probe`, with just-desktops running and the default `modifier` and `keys`.
- **Proof.** The table is the proof. Record it before and after a change and `diff` the two. With the current tap it should read `cmd-opt-1 pass`, `ctrl-opt-tab pass`, `opt-shift-lbracket pass`, `esc-not-armed pass`, `opt-1-autorepeat swallow`, `opt-shift-1-autorepeat swallow`, `opt-a pass`, `opt-tab swallow`, `opt-shift-tab swallow`, `esc-armed swallow`, `opt-tab-again swallow`, `esc-after-release pass`, `opt-lbracket swallow`, `opt-rbracket swallow`. `esc-after-release` reads `swallow` if releasing `⌥` no longer commits.
- **Seen.** The ten rows other than `opt-shift-1-autorepeat`, `opt-a`, `opt-tab-again` and `esc-after-release`, the same in three runs. Those four are read from `src/hotkey.c`.
- **Gotchas.** The table cannot see the call and quick-key timestamps, the Tab repeat throttle, or whether a repeat starts a switch: both ways the key is swallowed. With `switcher = list` or `square` the panel flashes during the held rows.
- **Gotchas.** A tap that is off (disabled by timeout) cannot be driven, and neither can the missed-release path in it.

## Hotkeys that do nothing

- **Reach.** Press the key of the desktop you are on.
- **Drive.** With an ordinary app in front, `jd-drive key 18` while on Desktop 1, and `jd-drive chord optshift 18` (a move to the desktop the window is on).
- **Proof.** No `os_log` line and no change on screen.
- **Gotchas.** The first one does act when the frontmost app is set to `none` (`wm_dismiss_every`): it hides the app, with an `app=... reason=hide trigger=hotkey` line. Run it once with such an app in front and read the log.

## Menu bar strip and the "+k" overflow

- **Reach.** Open more windows than the strip has slots, with `menubar = on`.
- **Drive.** Open windows with `open -na TextEdit` repeatedly until the strip ends in "+k", then `jd-drive click <x> <y>` on the "+k" slot (take the point from a screenshot), then `jd-drive click` on a row, then on the desktop.
- **Proof.** A screenshot of the strip shows the current desktop's key and a glyph per window, and "+k" with k the count of windows without a glyph. After the click a panel lists them. A click elsewhere closes it. A click on a row focuses that window. Clicking a window in the strip focuses it and may switch desktops (`trigger=menubar`).
- **Gotchas.** A posted click is not hardware input, so what the app does with input timing differs from a real click. The status item is drawn by the system: take the screenshot of the whole menu bar.

## Quit and restart with the desktops kept

- **Reach.** Quit the app, then start it within 60 seconds.
- **Drive.** Put a window on Desktop 2 with `jd-drive chord optshift 19`. Follow "Launch" in SKILL.md: `kill -TERM <pid>`, wait for the exit, `open <app>`.
- **Proof.** One `reason=restore-file` line per window of the old run, and the moved window is on Desktop 2 and not on screen on Desktop 1. After more than 60 seconds the lines read `reason=track` and every window is on the current desktop.

## New window, close, minimize, app launch and quit

- **Reach.** `⌘N`, `⌘W`, `⌘M`, open an app, quit an app, each on a desktop other than 1.
- **Drive.** `jd-drive key 19`, then `osascript -e 'tell application "System Events" to keystroke "n" using command down'` in TextEdit, `... keystroke "m" using command down`, `... keystroke "w" using command down`, `open -a TextEdit`, `osascript -e 'quit app "TextEdit"'`. System Events needs the terminal's Automation grant.
- **Proof.** A new window gets `reason=track` with `ws` the current desktop. A close, minimize or quit starts a quiet period and the focus afterwards logs `reason=refocus` or `left`, not `follow`: the user stays on the desktop. The `⌘W`, `⌘Q`, `⌘M` and `⌘H` keys are the ones that do not take the quick path (see `keyrules_is_quick_key`).
- **Gotchas.** Whether the quiet period was entered shows only as `settling=1` on the next line, so read that field.

## An app set to `none`

- **Reach.** Set `<bundle id> = none` for one app, restart, switch desktops.
- **Drive.** `jd-drive key 19`, then `jd-drive key 18`. Open the app first.
- **Proof.** One `app=... reason=hide trigger=hotkey cur=N` line for each switch, and the app's windows are hidden (not on screen in `jd-drive windows`'s owner list) on every desktop. Calling it with its own shortcut shows it in place, with no desktop switch.

## Not driven automatically

- A `reason=call` line from a click. A posted click does not count as input for just-desktops. A posted key does reach the tap, but no journey here produces `call` with one yet. Ask the user to click.
- A display being added, removed or rearranged (`repark`). It needs hardware or System Settings.
- An app that has hung. Accessibility calls to it time out, and a test app that hangs on purpose is not in this repo.
- The tap being disabled by the system, and a `⌥` release missed in that time.

## Not driven yet

The `⌥Tab` Esc cancel by posting (`chord` cannot send Esc inside the held modifier), and Show Desktop and App Exposé against the switchers. Add a section here when you drive one.
