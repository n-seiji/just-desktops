# The app

Install it with `brew trust n-seiji/tap` (once) and `brew install --cask n-seiji/tap/just-desktops`, or download `just-desktops-<version>.app.zip` from [Releases](https://github.com/n-seiji/just-desktops/releases/latest). Releases are signed with a Developer ID and notarized by Apple. To build it yourself, run `mise run app` (it lands in `dist/just-desktops.app`).

## First launch

1. Open the app. It has no Dock icon. Its item is always in the menu bar.
2. The first run explains the Accessibility permission. Click Open System Settings and turn it on. The app starts as soon as you do.
3. Screen Recording is asked for only when the switcher is `square`.

## The menu

Click the menu bar item (right-click on the windows strip) for the menu: switcher style, focus border, menu bar style, Launch at Login, Open Config File and Quit. Choosing a setting writes that one line of the config and restarts the app. Windows parked on other desktops come back to the screen first, then return to their desktops when the app is up again. Keys, modifier, pins and `no_preview` are edited in the config file.

## Signing and permissions

Releases signed with a Developer ID and notarized open normally, and keep their permissions across updates.

An ad-hoc signed build (one you built yourself) is identified by its code hash, so macOS asks for the permissions again after every rebuild. If Gatekeeper blocks it, open System Settings > Privacy & Security and choose Open Anyway for this app only. Do not turn Gatekeeper off with `spctl --master-disable`, and do not strip quarantine from other apps with `xattr -cr`.

To check a downloaded release, compare `shasum -a 256 just-desktops-<version>.app.zip` with the `.sha256` file on the release, or run `gh attestation verify just-desktops-<version>.app.zip --repo n-seiji/just-desktops`.

## App and service together

The app and the launchd service from the CLI install must not run together. The second instance exits at once.
