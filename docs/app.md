# The app

To install a release, see [Install](../README.md#app) in the README. To build it yourself, run `mise run app` (it lands in `dist/just-desktops.app`).

## First launch

1. Open the app. It has no Dock icon. Its item is always in the menu bar.
2. The first run explains the Accessibility permission. Click Open System Settings and turn it on. The app starts as soon as you do.
3. Screen Recording is asked for only when the switcher is `square`.

The menu bar item opens the app's menu. [Menu bar](menubar.md#the-menu) describes it.

## Signing and permissions

Releases signed with a Developer ID and notarized open normally, and keep their permissions across updates.

An ad-hoc signed build (one you built yourself) is identified by its code hash, so macOS asks for the permissions again after every rebuild. If Gatekeeper blocks it, open System Settings > Privacy & Security and choose Open Anyway for this app only. Do not turn Gatekeeper off with `spctl --master-disable`, and do not strip quarantine from other apps with `xattr -cr`.

To check a downloaded release, compare `shasum -a 256 just-desktops-<version>.app.zip` with the `.sha256` file on the release, or run `gh attestation verify just-desktops-<version>.app.zip --repo n-seiji/just-desktops`.
