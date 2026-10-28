# Menu bar

An optional menu bar item shows one glyph per window on this desktop, and the current desktop's key in a box.

| `menubar` | Item |
|---|---|
| `on` (or `strip`) | The window strip described below |
| `icon` | Only a small template app icon (an outlined rounded square with a filled one inside) |
| `off` | None. Command line only |

The command line default is `off`. The app always has an item: its default is `on`, and `off` behaves like `icon`.

![The menu bar item](images/menubar.png)

## The strip

- One monochrome glyph per window. Two Finder windows show two Finder glyphs.
- Glyphs are grouped by app. An app's windows stay in a fixed order, so changing focus never reshuffles them. The [switcher](switcher.md) lists windows in the same order.
- The current desktop's key sits at the right end in an outlined box. The item always has room for 5 glyphs, with the strip right-aligned, so switching between desktops with up to 5 slots never changes its width (macOS animates the whole menu bar on a width change). With more it widens at once and narrows again after 30 s without needing the extra room.
- The focused window's glyph (only that one, not every window of its app) is cut out of a filled tile.
- The glyphs are template images, so macOS tints them for light and dark menu bars.
- It redraws only when the desktop, the focused window or the set of windows changes.

## Clicking

- Click a glyph to focus exactly that window.
- Clicking the key box or a gap between glyphs does nothing.
- Right-click or control-click the item for the menu. The item has no permanent menu, so left clicks are never swallowed. In `icon` mode every click opens it.

## The menu

The bare binary's menu has only Quit. The app's menu has:

- **Switcher**: List, Square, Instant (off), with a check on the current one.
- **Focus border**: a toggle.
- **Menu bar**: Windows strip or Icon only.
- **Launch at Login**: a toggle (a login item registered through macOS). A check means on. If macOS wants approval, the item shows a dash and System Settings > General > Login Items opens.
- **Open Config File**: creates `~/.config/just-desktops/config` from a commented template if it is missing, then opens it. Keys, modifier, pins and `no_preview` are edited there.
- **About just-desktops** (opens the project page) and **Quit just-desktops**.

Choosing a setting rewrites only that key in the config file (the line is replaced or appended; other lines and comments stay; a symlinked config stays a symlink), then restarts the app. The restart puts parked windows back on the screen and saves which desktop each belongs to, then starts a new process that puts them back on those desktops. Quitting and launching again within a minute does the same. Choosing Square asks for Screen Recording after the restart if it is not granted.

## The `+k` slot

The item shows at most 10 slots. With more than 10 windows it shows the first 9 and a `+k` slot that counts the rest.

Click `+k` to open a small panel under it. The panel lists the hidden windows in the same order, each as the app's icon and the window title (cleaned the same way as in the switcher). Click a row to focus that window.

- Down/Up or Tab/Shift+Tab move the highlight (wrapping).
- Return focuses the highlighted window.
- Esc, or a click anywhere else, closes the panel without changing focus.
- The panel closes by itself when the set of windows changes.
- It follows the system's light or dark appearance and shows at most 14 rows at a time. The mouse wheel scrolls longer lists.

## Memory

AppKit's status item costs about 5MB: idle is about 13.6MB with the strip and 12.6MB with `icon`. With `menubar = off` (command line only) none of it is loaded and idle stays about 8MB.

The panel is created on click and freed on close. Opening it once costs about 3MB of resident memory that stays after it closes (window and text machinery loaded on first use). Repeated opens do not add more.
