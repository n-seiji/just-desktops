<h1>
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/wordmark-dark.svg">
    <img src="assets/wordmark-light.svg" width="280" alt="just-desktops">
  </picture>
</h1>

<p><b>English</b> · <a href="README.ja.md">日本語</a></p>

Keyboard-driven desktops for macOS, in one small background process (about 8MB).

<table>
  <tr>
    <td align="center" valign="top" width="33%">
      <img src="assets/tile-windows.svg" width="160" alt="Three windows stay in place while the focus moves from one to the next"><br>
      <b>Switch windows</b><br>
      <sub>⌥Tab moves through the windows on this desktop.</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <img src="assets/tile-desktops.svg" width="160" alt="A screen whose windows change as ⌥1, ⌥2 and ⌥3 are pressed"><br>
      <b>Switch desktops</b><br>
      <sub>⌥1, ⌥2, ⌥3. Up to 10 desktops.</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <img src="assets/tile-none.svg" width="160" alt="One window follows you across desktops"><br>
      <b>Apps on every desktop</b><br>
      <sub>Choose apps that belong to every desktop and hide when you switch, such as a terminal or a password manager.</sub>
    </td>
</table>

Three ways to see the windows on this desktop:

<table>
  <tr>
    <td align="center" valign="top" width="33%">
      <img src="assets/tile-menubar.svg" width="160" alt="A menu bar with one icon per window and the desktop key; the highlight moves to the focused window"><br>
      <b>Menu bar</b><br>
      <sub>One icon per window, and the desktop key. Or just one small icon.</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <img src="assets/tile-list.svg" width="160" alt="A list of windows whose selection moves down as Tab is pressed"><br>
      <b>List</b><br>
      <sub>⌥Tab shows each window's app and title.</sub>
    </td>
    <td align="center" valign="top" width="33%">
      <img src="assets/tile-square.svg" width="160" alt="A large preview of one window that changes as Tab is pressed, with a row of dots below"><br>
      <b>Square</b><br>
      <sub>⌥Tab shows a large preview of each window. Or turn the panel off and Tab just jumps.</sub>
    </td>
  </tr>
</table>

- **Desktops.** Press `⌥1`, `⌥2`, `⌥3`... to switch between up to 10 desktops. Add Shift to take the focused window with you. macOS Spaces and SIP are left alone.
- **Window switcher.** Hold `⌥` and press Tab to pick a window on this desktop. Shown as a list, a big preview, or not at all.
- **Menu bar.** Optional. One icon per window on this desktop, and the desktop number.

<img src="assets/tile-border.svg" width="96" align="right" alt="A ring around the focused window">

**Also: a focus ring.** A thin ring around the window you are typing in, so you can see where your keys go. It can be turned off.

<br clear="right">

## What it doesn't do

<img src="assets/tile-noresize.svg" width="96" align="right" alt="Windows keep their size">

- **No resizing, tiling or layouts.** Windows keep their size and position. Use Raycast or another tool for that, or [AeroSpace](https://github.com/nikitabobko/AeroSpace) for tiling.
- **No colors or styles for the border.** For that, use [JankyBorders](https://github.com/FelixKratz/JankyBorders).

<br clear="right">

## Install

### App

```sh
brew trust n-seiji/tap        # once: Homebrew only loads casks from third-party taps you trust
brew install --cask n-seiji/tap/just-desktops
```

Or download `just-desktops-<version>.app.zip` from [Releases](https://github.com/n-seiji/just-desktops/releases/latest) and move `just-desktops.app` to Applications. It is signed with a Developer ID and notarized by Apple.

Open it and allow Accessibility when macOS asks. It lives in the menu bar, with no Dock icon. Right-click its item for settings, including Launch at Login. More in [docs/app.md](docs/app.md).

### Command line

```sh
mise use -g github:n-seiji/just-desktops
just-desktops
```

macOS asks for Accessibility. Grant it to `just-desktops` itself, not to your terminal: a terminal with Accessibility lets every program started in it drive your apps. Run it through `mise run service` (below) so that only the binary holds the grant. On exit, every parked window is moved back.

To start at login, run this inside the repository:

```sh
mise run service     # register and start a LaunchAgent
mise run unservice   # stop and unregister it
```

Upgrading through mise changes the binary path. Run `mise run service` again and re-grant Accessibility.

The service writes its log to `~/Library/Logs/just-desktops.log` (earlier versions used `/tmp/just-desktops.log`; `mise run service` rewrites the plist, and the old file can be deleted).

Use either the app or the command line service, not both: the second one exits at once.

## Quick start

- Hold the modifier (Option) and press `1`, `2`, `3`, `Q`, `W` or `E` to go to Desktop 1 to 6.
- Add Shift to move the focused window to that desktop.
- Hold Option and press Tab to pick a window on this desktop. Release Option to go there.
- The config is `~/.config/just-desktops/config`. All options are optional. Restart to apply changes.

## Keys

`⌥` is the base modifier. It is Option unless you set `modifier`.

| Key | What it does |
|---|---|
| `⌥1` `⌥2` `⌥3` `⌥Q` `⌥W` `⌥E` | Switch to Desktop 1 to 6 |
| `⌥⇧` + the same key | Move the focused window to that desktop |
| `⌥[` / `⌥]` | Go back / forward through the last 10 focused windows, across desktops |
| `⌥Tab` / `⌥⇧Tab` | Select the next / previous window. Wraps around |
| Release `⌥` | Switch to the selected window |
| `Esc` | Cancel the switcher |

## Configuration

File: `~/.config/just-desktops/config`. See [config.example](config.example).

| Option | Values | Default | What it does |
|---|---|---|---|
| `border` | `on`, `off` | `on` | Ring around the focused window. `off` creates no border window |
| `switcher` | `list`, `square`, `off` | `list` | Tab switcher style. `on` means `list`, unknown values too |
| `menubar` | `on` (or `strip`), `icon`, `off` | `off` (app: `on`) | Menu bar item: one glyph per window, or only a small app icon. The app always has an item, so `off` means `icon` there |
| `keys` | up to 10 digits or letters | `1 2 3 q w e` | The n-th key is Desktop n |
| `modifier` | `opt`, `cmd`, `ctrl` | `opt` | Base modifier of every hotkey. See [modifier](docs/workspaces.md#modifier) |
| `no_preview` | bundle ids | — | Apps the square switcher never screenshots. Repeatable |
| `<bundle id>` | `<n>`, `none` | — | Pin an app to Desktop n. `none` puts the app on every desktop and hides it when you switch desktops |

```
switcher = square
menubar = on
com.apple.Safari = 1
```

## Details

- [Switcher](docs/switcher.md): the three styles, the delay, Screen Recording, password managers.
- [Menu bar](docs/menubar.md): glyphs, the `+k` panel, clicking.
- [Workspaces](docs/workspaces.md): focus history, pins, apps on every desktop, following windows, the border, modifier conflicts.
- [App](docs/app.md): the menu bar app, its menu and permissions.

## Design

- **One process.** Workspaces, the switcher and the border live in one daemon.
- **Mostly C.** The Objective-C is `src/apps.m` (app launch, quit and activation events) and `src/menubar*.m` (the status item and its panel and menu, created only when `menubar` is not `off`; `src/app.m` is the app's first-run panel, login item and config helpers).
- **Border through SkyLight**, like JankyBorders, so no NSWindow is created.
- **Hotkeys through a CGEventTap** on the main run loop. No extra threads.

It mixes two tools: [AeroSpace](https://github.com/nikitabobko/AeroSpace) (windows on inactive workspaces are parked in a corner) and [JankyBorders](https://github.com/FelixKratz/JankyBorders) (the border).

## Memory

Measured with `footprint`.

| State | Memory |
|---|---|
| Idle, `menubar = off` | about 8MB |
| Idle, `menubar = on` | about 13.6MB |
| `list` open | peak about 16MB |
| `square` open | peak about 25MB |

The switcher memory is released when you let go of `⌥`. More in [switcher](docs/switcher.md#memory) and [menu bar](docs/menubar.md#memory).

## Development

Needs the Xcode Command Line Tools and [mise](https://mise.jdx.dev). Use `mise run build`, `mise run run` (build and run in the foreground), `mise run test` and `mise run test-san` (the same tests under the address and undefined behaviour sanitizers). `scripts/footprint.sh [config-file] [seconds]` prints the idle footprint in bytes; stop any running instance first. Merging a `JD_VERSION` change to `main` builds a release, which is published after approval ([docs/releasing.md](docs/releasing.md)). [docs/architecture.md](docs/architecture.md) maps the code: modules, data flow, threading and the memory rules.

## Limitations

- Windows are parked on the main display only. Multiple displays are not supported yet.
- SkyLight is a private Apple API, so a macOS update can break the border.
- After a crash, parked windows stay in the bottom-right corner until the next launch moves them back.
- Only the desktop keys and the base modifier are configurable.

## License

[GPL-3.0](LICENSE).

## Support

If just-desktops saves you a few keystrokes a day, you can [buy me a coffee](https://buymeacoffee.com/sijis).
