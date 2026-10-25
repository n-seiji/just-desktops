# Releasing

Merging a change to `JD_VERSION` in `src/common.h` into `main` runs `.github/workflows/release.yml`. The `check` job skips the run if `vX.Y.Z` already has a release, a draft or a git tag. Otherwise the `build` job builds the CLI tarballs and the app zip and stores them in a draft release. After you approve the `release` environment, the `publish` job verifies the checksums, attests the files and publishes the release. GitHub creates the `vX.Y.Z` tag at that point, on the merged commit. Nobody tags by hand.

A release is always Developer ID signed and notarized: without all five secrets below (in the `signing` environment) the `build` job fails before building. The workflow signs with a Developer ID Application certificate, notarizes, staples the app and re-zips it, then computes the checksums from the final files. The attestations are made from those same files after approval.

## One-time setup

You need an Apple Developer Program membership.

### 1. Developer ID Application certificate

1. Keychain Access > Certificate Assistant > Request a Certificate From a Certificate Authority. Save the CSR to disk.
2. developer.apple.com > Certificates, IDs & Profiles > Certificates > + > Developer ID Application. Choose the G2 Sub-CA, upload the CSR, download the `.cer` and open it so it lands in your login keychain.
3. In Keychain Access > My Certificates, export "Developer ID Application: ..." as `cert.p12` with a password.
4. `base64 -i cert.p12 | pbcopy`, or keep the file for the `gh` command below.

The certificate carries your legal name, and it is shown in the Gatekeeper dialog and in `codesign -dvv`.

### 2. App Store Connect API key

1. appstoreconnect.apple.com > Users and Access > Integrations > App Store Connect API > Team Keys > +. Give it the Developer role.
2. Download `AuthKey_<KEYID>.p8`. Apple lets you download it once.
3. Note the Key ID and the Issuer ID shown on that page.

### 3. Create the environments

```sh
scripts/setup-release-env.sh
```

Run it first, and again after the change that moved the release trigger to `main` merges. It creates two environments, both deployable from the `main` branch only (it replaces the old `v*` tag policy):

- `signing` holds the signing and notarization secrets for the `build` job. It has no reviewers.
- `release` gates the `publish` job, with you as the required reviewer.

It is safe to re-run. Set `REVIEWER` or pass a login to change the reviewer, and `REPO` to change the repository. Without it GitHub creates an environment on first use with no reviewers and no branch policy, and `publish` runs unapproved. Check it:

```sh
gh api repos/n-seiji/just-desktops/environments/signing -q '.protection_rules'
gh api repos/n-seiji/just-desktops/environments/release -q '.protection_rules'
gh api repos/n-seiji/just-desktops/environments/signing/deployment-branch-policies -q '.branch_policies'
gh api repos/n-seiji/just-desktops/environments/release/deployment-branch-policies -q '.branch_policies'
```

### 4. Environment secrets

Run these from the repository, with the real files and values. Nothing is echoed. The secrets belong to the `signing` environment, which only jobs running on `main` can use, so workflows on other branches cannot read them.

```sh
base64 -i cert.p12 | gh secret set DEVELOPER_ID_P12_BASE64 --env signing
gh secret set DEVELOPER_ID_P12_PASSWORD --env signing   # prompts, input hidden
base64 -i AuthKey_ABC123DEFG.p8 | gh secret set ASC_KEY_P8_BASE64 --env signing
gh secret set ASC_KEY_ID --env signing                  # prompts: the Key ID
gh secret set ASC_ISSUER_ID --env signing               # prompts: the Issuer ID
```

Delete `cert.p12` and the `.p8` afterwards, or store them in a password manager.

The release fails before building unless all five are set.

Use a dedicated App Store Connect API key, with the Developer role, for this repository (step 2), not one shared with other repositories.

### 5. Delete the old repository secrets

If the five secrets were ever set at repository scope, delete the repository-level ones right after the five `signing` environment secrets are set. Do not wait for a release.

Jobs that run in an environment can also read repository-level secrets. So while those secrets exist, the environment restriction (deployable from `main` only) protects nothing: any workflow can still read them.

```sh
gh secret list                  # repository-level secrets: none of the five should remain
gh secret list --env signing    # the five environment secrets
gh secret delete DEVELOPER_ID_P12_BASE64
gh secret delete DEVELOPER_ID_P12_PASSWORD
gh secret delete ASC_KEY_ID
gh secret delete ASC_ISSUER_ID
gh secret delete ASC_KEY_P8_BASE64
```

## Cutting a release

1. Set `JD_VERSION` in `src/common.h` to the new version (`X.Y.Z`) in a PR and merge it to `main`.
2. `check` finds no release or tag for `vX.Y.Z` and starts `build`. A `common.h` change that leaves `JD_VERSION` alone runs `check` only, and it skips the rest. If the current version has no release yet, any `common.h` change starts one; the approval gate makes that harmless.
3. `build` signs, notarizes and uploads a draft release. Review the draft under Releases: assets and notes. Optionally download it and run the checks below.
4. In the Actions run, open "Review deployments" and approve `release`. `publish` verifies the checksums, attests the files and publishes the release.
5. Update the cask, after approval (it verifies the attestation). The cask lives in [n-seiji/homebrew-tap](https://github.com/n-seiji/homebrew-tap); `packaging/homebrew/just-desktops.rb` is its template here.

   ```sh
   scripts/bump-cask.sh X.Y.Z                                    # the template in this repo
   scripts/bump-cask.sh X.Y.Z ../homebrew-tap/Casks/just-desktops.rb   # a clone of the tap
   ```

   Commit and push the tap, then `brew audit --cask --online n-seiji/tap/just-desktops`.

The first release of a version that is already set (no release, draft or tag yet): Actions > release > Run workflow, on `main`. Dispatch from any other ref fails in `check`.

### What is public before approval

Public: the Actions logs. The workflow masks the signer's name and Team ID in them; they become public with the release, inside the signed app.
Not public: the tag, the binaries, the attestation and the release page. The draft is visible only to people with write access. The tag, the release and the attestation appear after approval.

### Reject, re-run, expiry

- Reject: reject the run in "Review deployments" (or cancel it), then `gh release delete vX.Y.Z --yes`. The tag does not exist yet, so there is nothing else to clean up. Then Actions > release > Run workflow on `main` starts it again.
- A run waiting for approval holds the `release` concurrency group until you approve, reject or it expires (30 days). Meanwhile only the newest later run waits; older waiting ones are cancelled, which loses nothing because each run reads the version at its own commit.
- After an expiry the draft stays and a new run would skip it: delete the draft (`gh release delete vX.Y.Z --yes`), then Run workflow on `main`.
- "Re-run all jobs" does nothing: `check` sees the draft and skips `build`. Re-run only the failed `publish` job, or delete the draft and start a new run.
- If `build` fails after creating the draft (an asset upload, say), re-running it stops at its guard. Delete the draft first.
- A run uses the `release.yml` of the commit that started it. If `publish` is broken, delete the draft, fix `main`, and run the workflow again.
- `publish` uses `GITHUB_TOKEN`, so the release does not trigger workflows on `release: published`.
- The attestation is made on the ubuntu runner after approval, not on the macOS runner that built the files. `publish` first checks the draft's files against the checksums `build` passed along, so it attests exactly what `build` made.
- `build` and `publish` each fail if the release or the tag appeared in the meantime. `publish` checks the tag right before publishing.

## Checking the result

```sh
gh release download vX.Y.Z --pattern '*.app.zip' --dir /tmp/jd && cd /tmp/jd
ditto -x -k just-desktops-X.Y.Z.app.zip .
codesign --verify --deep --strict --verbose=2 just-desktops.app
xcrun stapler validate just-desktops.app
spctl --assess --type execute --verbose=4 just-desktops.app   # source=Notarized Developer ID
```

The workflow runs the same checks and fails if the app is not accepted as notarized.

## Good to know

- The CLI binaries are signed and notarized but cannot carry a ticket, so Gatekeeper checks them online on first run.
- If the membership lapses, releases that are already notarized keep working. You cannot sign or notarize new ones.
- The designated requirement of a Developer ID app is based on the Team ID and bundle identifier, not the code hash. Accessibility and Screen Recording grants therefore persist across updates.
- To notarize by hand: `ASC_KEY_ID=... ASC_ISSUER_ID=... ASC_KEY_P8=path/to/AuthKey.p8 scripts/notarize.sh dist/just-desktops-X.Y.Z.app.zip`.
