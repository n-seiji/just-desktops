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
CODESIGN_IDENTITY="$id" mise run app          # dist/just-desktops.app
pgrep -fl just-desktops                        # note the path of the instance you are about to replace
for p in $(pgrep -x just-desktops); do kill -TERM "$p"; done   # a clean quit: it saves desktops and unparks
dist/just-desktops.app/Contents/MacOS/just-desktops > "$TMPDIR/jd-run.log" 2>&1 &
```

Start the new instance within 60 seconds of the quit and every window returns to its desktop (see "Leaving and restarting" in `docs/architecture.md`). With no Developer ID in the keychain, stop and tell the user: do not grant Accessibility to a terminal or an ad-hoc binary for them.

This takes over the user's screen, mouse and keyboard for a few seconds per run. Say so before you start.

## Doctor

```sh
pgrep -fl just-desktops    # exactly one, at the path you launched
/usr/bin/log show --last 1m --predicate 'subsystem == "dev.n-seiji.just-desktops"' --style compact | tail
```

A healthy start logs one `reason=track trigger=start` or `reason=restore-file` line per window. No lines means no Accessibility grant.

## Drive

Build the helper once per session:

```sh
clang .claude/skills/verify-just-desktops/drive.c -o "$TMPDIR/jd-drive" -framework ApplicationServices
```

| Command | What it does |
| --- | --- |
| `jd-drive key 18` | Presses `⌥1` (18 is the key code of `1`). The desktop keys are in `~/.config/just-desktops/config`. |
| `jd-drive click <x> <y>` | Left click, in global display points. |
| `jd-drive mc` | Prints `1` while Mission Control is up or zooming out. |
| `jd-drive windows` | Lists on-screen windows above layer 0. |
| `open -a "Mission Control"` | Toggles Mission Control. |

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

Kill only the instance you started, by its pid, then put the user's instance back and check it is the one running:

```sh
kill -TERM <pid you started>
open <path noted in Launch>            # by path: the test build shares the bundle id, so `open -b` may pick it
pgrep -fl just-desktops
```

If the user should keep the fixed build for now, copy it out of `dist/` first: `mise run app` deletes `dist/just-desktops.app` on every build. Tell them which build is running. Recordings and sheets stay in `$TMPDIR`.
