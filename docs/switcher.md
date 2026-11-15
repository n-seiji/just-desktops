# Switcher

The Tab switcher moves between the windows of the current desktop. It has three styles, set with `switcher` in the config. The keys are the same in all of them.

![The square switcher](images/switcher-square.png)

## Keys

- `⌥Tab` and `⌥⇧Tab` select the next and previous window. Release `⌥` to switch. `Esc` cancels.
- just-desktops always captures `⌥Tab` and `⌥⇧Tab`, so apps never see them.
- `⌥` is the base modifier. See [modifier](workspaces.md#modifier).
- The list holds only windows that are really shown. Windows of other Spaces, hidden apps and windows parked off screen (such as Raycast's idle window) are left out.
- The list is in the [menu bar](menubar.md#the-strip)'s order: grouped by app in order of each app's first window, an app's windows in a fixed order.

## Styles

| Style | What you get |
|---|---|
| `list` (default) | A panel with one row per window. |
| `square` | A wide glass panel (640x420pt) in the middle of the screen. It shows one window large (its screenshot, or the app icon until there is one), a `k / n` counter and a row of dots. No window name is shown. `⌥Tab` slides the next window in from the right, `⌥⇧Tab` from the left. A tall or narrow window is fitted to the picture box and centred, not stretched. |
| `off` | No panel. See below. |

`switcher = on` means `list`. An unknown value, including `peek`, which no longer exists, is ignored with a warning on stderr, and the default (`list`) or an earlier valid line stays.

In `list`, a row shows the app name when the app has one window. With several windows, rows show the window titles ("App 2" when a title is empty or repeated).

### Instant mode (`off`)

The first `⌥Tab` takes the same window list the panels use and focuses the next window at once. Every further Tab while `⌥` is held steps through that same list in order (`⌥⇧Tab` goes backwards), so cycling does not bounce between two windows. Releasing `⌥` ends the cycle. Nothing is drawn or allocated, and the idle footprint stays as it is.

## The delayed panel

The panel exists only while `⌥` is held after a Tab. It appears once `⌥` has been held for about 150ms. A quick tap of `⌥Tab` switches to the next window without drawing the panel, so no window or buffer is created. Extra Tabs, `Esc` and releasing `⌥` inside that time work the same. Nothing is held in memory while the panel is closed.

A held Tab repeats at the OS rate (about 30ms), which is faster than a step is worth. A held Tab steps at most once every 120ms, and only after the last step is done. The extra repeats are swallowed, so the selection does not race around or keep moving after you release.

## Screen Recording (`square`)

`square` shows a live screenshot of each window. This needs Screen Recording permission. just-desktops asks once at launch, and only for this style. `list` and `off` never ask.

- Until you grant it, the cards show the app icon. Everything else works the same.
- Only the selected window and the one it slides in from are captured, as you move. The images are dropped as soon as they are drawn.
- The grant of an ad-hoc signed binary is tied to its code hash. After a rebuild (`mise run build`, or an upgrade through mise), macOS asks again.

### Password managers

Windows of password managers are never captured. They show the app icon, the same placeholder as without permission. The built-in list covers 1Password (all versions), Bitwarden, Dashlane, LastPass, KeePassXC, KeePassium, Strongbox, Enpass, NordPass, Proton Pass, Keeper, MacPass, Buttercup, Apple Passwords and Keychain Access.

Other apps, for example RoboForm or Secrets, are not built in. Add them with `no_preview` in the config:

```
no_preview = com.example.vault com.example.notes
```

The line can repeat. Ids are added to the built-in list and matched case-insensitively.

## Memory

Measured with `footprint` while `⌥` is held, with screenshots:

| Style | Peak |
|---|---|
| `list` | about 16MB |
| `square` | about 25MB |

All of it is released when you let go of `⌥`. The idle footprint (about 8MB) does not depend on the style.

The cards are drawn at 1x, so their screenshots are a little soft. At 2x the wide cards would add about 15MB.

ScreenCaptureKit is linked weakly, so just-desktops still starts on macOS 13. The system maps the framework at launch whatever the style (checked with `DYLD_PRINT_LIBRARIES=1`). Its code runs only when you choose `square`. A build without the framework idled the same as the normal one within run-to-run noise: about 8MB with `menubar = off` and 13.6MB with `menubar = on` (4 to 6 runs each).
