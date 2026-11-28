---
name: verify-just-desktops
description: Build, test and drive just-desktops (the macOS desktop switcher in this repo) on the real desktop and capture proof. Use after any change under src/, before saying a change works, and to reproduce a visual bug such as a glitch during a desktop switch or in Mission Control.
---

# Verify just-desktops

The unit tests cover only the pure modules. Everything in `wm*.c`, `switcher*.c`, `hotkey.c`, `border.c`, `menubar*.m` and `main.m` is proved only by driving the running app. Paths below are relative to the repo root.

## Gate

```sh
.claude/skills/verify-just-desktops/gate.sh
```

It runs what CI runs (`build`, `test`, `test-san`, `--version`, `app`) and counts compiler warnings, because the build has no `-Werror`. A pass prints `build=0 test=0 san=0 version=0 app=0 warnings=0 <version>` and exits 0. Run it before and after a change and compare the lines. Logs are in `$TMPDIR/jd-gate`.

Scripts need `CFLAGS` from `mise.toml`, so always go through `mise run <task>`. A new test needs its own `run <name> src/<file>.c` line in `scripts/run-tests.sh`. Nothing discovers tests.

## Launch

Only one instance runs: a second one exits on the lock. The user normally has the released app running, so you have to replace it for the test and put it back after.

An ad-hoc build has no Accessibility grant and macOS asks again after every rebuild. Sign the app with the user's Developer ID instead. It then has the same bundle id and team as the released app and inherits its grants.

```sh
id=$(security find-identity -v -p codesigning | sed -n 's/.*"\(Developer ID Application: .*\)"/\1/p' | head -1)
CODESIGN_IDENTITY="$id" mise run app
rm -rf "$TMPDIR/jd-test" && mkdir "$TMPDIR/jd-test" && cp -R dist/just-desktops.app "$TMPDIR/jd-test/"
ps -axo pid=,comm= | grep '/just-desktops$'     # note the pid and the .app (or binary) path you are about to replace
kill -TERM <that pid>                            # a clean quit: it saves desktops and unparks
while pgrep -x just-desktops >/dev/null; do sleep 0.1; done
open "$TMPDIR/jd-test/just-desktops.app"
```

- Run the copy, not `dist/`: `mise run app` and the gate delete `dist/just-desktops.app` on every build and leave an ad-hoc one.
- Wait for the old instance to exit. It holds the lock for up to 2.5 seconds while it unparks, and a new one started meanwhile exits at once.
- Start it with `open`, so that macOS checks the app's own grant. Started from a shell, it would run on the terminal's.
- Start it within 60 seconds of the quit and every window returns to its desktop (see "Leaving and restarting" in `docs/architecture.md`).
- With no Developer ID in the keychain, stop and tell the user. Do not grant Accessibility to an ad-hoc binary for them.
- If the user runs the CLI as a LaunchAgent (`launchctl print gui/$(id -u)/com.just-desktops` finds it), stop it with `mise run unservice` and put it back with `mise run service`.

This takes over the user's screen, mouse and keyboard for a few seconds per run. Say so before you start.

## Doctor

```sh
ps -axo pid=,comm= | grep '/just-desktops$'    # exactly one, at the path you launched
/usr/bin/log show --last 1m --predicate 'subsystem == "dev.n-seiji.just-desktops"' --style compact | tail
```

A healthy start logs one `reason=track trigger=start` or `reason=restore-file` line per window. No lines means no Accessibility grant.

## Drive

Build the helper once per session. It posts keys and clicks, so the terminal you run it from needs Accessibility. If it has none, ask the user: that grant is theirs to give.

```sh
clang .claude/skills/verify-just-desktops/drive.c -o "$TMPDIR/jd-drive" -framework ApplicationServices
```

| Command | What it does |
| --- | --- |
| `jd-drive key 18` | Presses `⌥1` (18 is the key code of `1`). It always sends `⌥`, so it fits only the default `modifier`. The desktop keys are in `~/.config/just-desktops/config`. |
| `jd-drive chord <opt\|optshift> <keycode> [-n steps] [-t hold_ms] [-a]` | Presses `⌥` (or `⌥⇧`) as a real flags-changed event, presses the key `steps` times with it held (default 1), waits `hold_ms` (default 0), then releases with flags 0. `-a` sends the key as one held key: the first press, then autorepeats at 30 ms. `hold_ms` 0 is the quick tap that commits a switcher at once. The `⌥Tab` panel opens after 150 ms from the first Tab, so use `-t 300` to see it. SIGINT and SIGTERM still release the modifier. Tab is 48, `[` 33, `]` 30, Esc 53. |
| `jd-drive probe` | Prints one `<row> pass\|swallow` line per key event of a fixed table: whether the key-down got past just-desktops's tap. Run it with just-desktops running, the default `modifier` and the default `keys`. See below. |
| `jd-drive click <x> <y>` | Left click, in global display points. |
| `jd-drive mc` | Prints `1` while Mission Control is up or zooming out. |
| `jd-drive windows` | Lists on-screen windows above layer 0. `windows all` adds layer 0, the app windows: a parked one shows as a sliver at the corner of the display furthest right. |
| `open -a "Mission Control"` | Toggles Mission Control. |

`probe` taps the session at the annotated point, after the app's own session tap, and tags its events with `kCGEventSourceUserData`, so your own typing is not counted. The tap drops the tagged events, so nothing it posts reaches the frontmost app. The terminal needs Accessibility, and Input Monitoring if macOS asks. It exits 1 with a message on stderr if the tap cannot be created. It leaves ⌥ up and Esc cancels the switcher, but it does act on the desktop: `⌥Tab` and `⌥⇧Tab` move the focus when `switcher = off`, and `⌥[` then `⌥]` go back and forward in the focus history. Run it with nothing important in flux, and compare two runs with `diff`.

Take click points from a screenshot or a frame of a recording, divided by the display scale (2 on a Retina display). In zsh, `log` is a builtin: call `/usr/bin/log`.

Journeys run so far are in [features/README.md](features/README.md).

## Evidence

Every desktop change writes one `os_log` line (`docs/architecture.md`, Diagnostics). The line proves which code path ran and when. A recording proves what the user saw. Capture both.

```sh
screencapture -x -v -V 4 "$TMPDIR/jd-run.mov" &      # 4 seconds of video
# ... drive ...
wait $!
ffmpeg -v error -y -ss 1.9 -t 1.0 -i "$TMPDIR/jd-run.mov" -vf "fps=16,scale=480:-1,tile=6x3" -update 1 "$TMPDIR/jd-run.jpg"
```

Read the contact sheet and count frames from the click to the settled screen. To compare two behaviours, record both with the same click and report the frame counts. `screencapture` needs Screen Recording for the terminal, and macOS may show the user a prompt.

## Cleanup

Quit the instance you started, by its pid, wait for it to exit, then put the user's instance back and check it is the one running:

```sh
kill -TERM <pid you started>
while pgrep -x just-desktops >/dev/null; do sleep 0.1; done
open <the .app noted in Launch>       # by path: the test build shares the bundle id, so `open -b` may pick it
ps -axo pid=,comm= | grep '/just-desktops$'
```

If the user wants to keep the test build running for now, leave it and tell them which build is running and where it is. Recordings and sheets stay in `$TMPDIR`.
