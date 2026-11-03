# dooblr installer

One app for macOS and Windows that installs and updates the whole dooblr suite. It shows every
plug-in with the version you have and the latest version, and you tick the ones to install or
update.

| Item | Kind | Goes into |
| --- | --- | --- |
| dooblr-q, dooblr-kick, dooblr-vocals, dooblr-prophet, dooblr-soothe, dooblr-feedback | VST3 | Mac: `/Library/Audio/Plug-Ins/VST3` (asks for your password once), or `~/Library/Audio/Plug-Ins/VST3` (just you).<br>Windows: `C:\Program Files\Common Files\VST3` (asks for permission, UAC), or `%LOCALAPPDATA%\Programs\Common\VST3` |
| dooblr-prophet panel (optional), dooblr-chords | Max for Live device (Ableton Live only) | `<Ableton User Library>/Presets/MIDI Effects/Max MIDI Effect/<device>`, the same folder the dooblr-chords and dooblr-prophet build scripts use, so Live's browser shows them under MIDI Effects > Max MIDI Effect |

Version 0.1.2.

## Download

- **macOS:** [dooblr-installer-mac.dmg](https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-installer-mac.dmg)
- **Windows:** [dooblr-installer-windows.exe](https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-installer-windows.exe)

These links work once a release has been published (see [Releasing](#releasing)). **The repository is
private right now**, so the links only work while you are signed in to GitHub with access, and the
installer needs a GitHub token to read the releases (below).

## First launch (private repository)

On first launch the Welcome page says **Sign in to GitHub to get the plug-in list** (GitHub answers
404 to anyone not signed in to a private repository). Once:

1. Click **Create a token on github.com** (or open <https://github.com/settings/personal-access-tokens/new>).
2. Fine-grained token: **Resource owner** Dooblr, **Repository access** > *Only select repositories* >
   **dooblr-plugins**, **Permissions** > Repository permissions > **Contents: Read-only**. Pick an
   expiry, click **Generate token** and copy it.
3. Paste it in the **GitHub token** box (it shows as dots) and click **Save and retry**.

The token is kept in the macOS Keychain (macOS may ask once whether the installer may use it: choose
**Always Allow**) or the Windows Credential Manager, and is never shown or written to the log. If
GitHub later refuses it (expired or revoked), the same box comes back with "GitHub did not accept the
saved token".

Where the installer looks for a token, in order: `DOOBLR_GITHUB_TOKEN`, `GITHUB_TOKEN`, `GH_TOKEN`,
the saved token, then `gh auth token` (the GitHub CLI, if you are signed in with `gh auth login`). It
tries without a token first, so nothing changes if the repository becomes public. With a token it
reads the release through the GitHub API (`api.github.com`) and sends the token only to GitHub's API,
never to the file host GitHub redirects downloads to. The sha256 check of every file is unchanged.

**Errors** are shown in a box you can select and copy from; **Copy details** copies the installer
version, the OS, the step, the address (with any secret removed), the HTTP status and the message,
ready to paste into a bug report.

## Using it

**macOS**

1. Open the `.dmg` and drag **dooblr installer** anywhere (or run it from the disk image).
2. The app is signed ad hoc, not notarised by Apple, so the first time macOS blocks it:
   - macOS 14 and earlier: right-click (or Control-click) the app, choose **Open**, then **Open** again.
   - macOS 15 (Sequoia) and later: double-click it once and close the warning, then open
     **System Settings > Privacy & Security**, scroll down and click **Open Anyway**.
   After that it opens normally.
3. Pick what to install or update and click **Install**.

**Windows**

1. Run `dooblr-installer-windows.exe`. It is not code-signed, so SmartScreen may show
   "Windows protected your PC": click **More info**, then **Run anyway**.
2. Pick what to install or update and click **Install**. Installing into
   `Common Files\VST3` asks for permission (UAC) once.

**The pages**

1. **Welcome**: reads the latest list of plug-ins (the manifest) and says how many updates there are.
2. **Choose**: one row per item with the installed and the available version and a tag: *Install*
   (not installed), *Update* (older version installed, or a version the installer can't read),
   *Up to date*, *Newer installed* or *Not available* (no file for this platform). Updates are
   ticked to start with. Click a row or its box to tick it; **Select updates**, **All** and
   **None** set the ticks; the folder menu switches between the system and the user VST3 folder.
3. **Install**: downloads each file, checks its size and sha256 against the manifest, and only
   then installs. A damaged or changed download is rejected and nothing of it is installed. An
   existing copy is replaced completely (no leftover files). **Cancel** stops before installing.
4. **Done**: what was installed and anything that failed, with the reason. Restart or rescan
   plug-ins in your DAW afterwards.

How it reads the installed version: from the VST3 bundle's `Info.plist` (Mac) or
`moduleinfo.json` (Windows), then from its own receipts (`~/Library/dooblr/installer-receipts.json`,
Windows `%APPDATA%\dooblr\installer-receipts.json`), and for Max for Live devices from the
`VERSION` / `UI_VERSION` line in the device's script.

## Command line

The same app runs without a window when given arguments (on Windows from a terminal):

```sh
APP="dooblr installer.app/Contents/MacOS/dooblr installer"
"$APP" --list                                   # installed / available / status for every item
"$APP" --install updates                        # install every update
"$APP" --install all
"$APP" --install dooblr-q,dooblr-soothe
"$APP" --list --manifest /path/to/dooblr-manifest.json   # a local or other manifest
"$APP" --install all --vst3-dir /tmp/v --m4l-dir /tmp/m   # other folders (tests)
"$APP" --save-token        # paste a GitHub token (not echoed); saved in the Keychain / Credential Manager
"$APP" --forget-token      # remove the saved token
"$APP" --list --no-auth    # never use a token (anonymous only)
```

Command-line runs work even while the installer window is open. On failure it prints the same
details as **Copy details** to stderr.

Environment: `DOOBLR_GITHUB_TOKEN` (or `GITHUB_TOKEN`, `GH_TOKEN`), `DOOBLR_MANIFEST_URL`, `DOOBLR_VST3_DIR`, `DOOBLR_M4L_DIR`, `DOOBLR_INSTALLER_RECEIPTS`.
Exit codes: 0 ok, 1 something failed, 2 bad arguments, 3 manifest could not be read,
4 GitHub sign-in needed (no token, or GitHub refused it).

## Building

```sh
dooblr-installer/scripts/build.sh   # builds in ~/dooblr-build/dooblr-installer, runs the tests, installs nothing
UNIVERSAL=1 dooblr-installer/scripts/build.sh            # arm64 + x86_64
DOOBLRINSTALLER_SNAPSHOT=/tmp/shot dooblr-installer/scripts/build.sh   # also writes window snapshots
```

JUCE 8.0.15 comes from `~/dooblr-build/_deps/juce-src` when it exists (`JUCE_SOURCE_DIR`), otherwise it is fetched.
The tests (`DooblrInstallerTests`) cover version compare, manifest parsing, sha256, reading
installed versions, a full install and update into a temp folder from `file://` zips, rejection
of corrupted, truncated and path-escaping zips, cancel, and the window's rows, ticks and pages.
They also cover GitHub sign-in against a local mock of the GitHub API: the token lookup order, the
saved-token file (user-only permissions), redaction of tokens and signed URLs, that the token is
never sent to the redirected file host, the 404 > sign-in > save > retry flow in the window, a
refused token, and the copied error text. They never read your real token (`DOOBLR_INSTALLER_NO_GH=1`,
`DOOBLR_INSTALLER_TOKEN_FILE=<temp file>`). Opt in: `DOOBLRINSTALLER_NETWORK_TESTS=1` also checks the
real anonymous answer from GitHub; `DOOBLRINSTALLER_TEST_KEYCHAIN=1` saves and removes a test item in
the Keychain.

`scripts/e2e-local.sh` (Mac) packages the plug-ins you have built in `~/dooblr-build`, lists them
against your real folders (read only), installs everything into a temp folder, checks the
signatures, and checks that a corrupted zip is rejected.

## Releasing

Releases are built by GitHub Actions (`.github/workflows/release.yml`):

```sh
git tag v0.1.2 && git push origin v0.1.2
```

The workflow builds every VST3 plug-in and the installer on macOS (universal, ad-hoc signed) and
Windows (x64, MSVC), packages the zips, builds `dooblr-chords`, adds the dooblr-prophet panel,
writes `dooblr-manifest.json` and publishes a GitHub Release with all of them. **Run workflow**
on the Actions tab builds the same files as a downloadable artifact without publishing.

By hand (Mac only), with the plug-ins built in `~/dooblr-build`:

```sh
python3 dooblr-installer/scripts/make-release.py package --platform mac --out OUT --build-root ~/dooblr-build --m4l --require-all
dooblr-installer/scripts/package-mac.sh "~/dooblr-build/dooblr-installer/DooblrInstaller_artefacts/Release/dooblr installer.app" OUT
python3 dooblr-installer/scripts/make-release.py manifest --out OUT --installer-mac OUT/dooblr-installer-mac.dmg OUT/partial-mac.json
```

Versions come from each plug-in's `CMakeLists.txt`, dooblr-chords' `package.json` and the
prophet panel's `UI_VERSION`. Asset names: `<id>-<version>-mac.zip`, `-windows.zip`, `-m4l.zip`.

## Changing the download address

The installer reads `https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-manifest.json`.
The manifest's file links are relative to it, so moving every release file to another host just
needs the new manifest address:

- at build time: `-DDOOBLRINSTALLER_MANIFEST_URL=https://example.com/dooblr/dooblr-manifest.json`
  (or the `DOOBLRINSTALLER_MANIFEST_URL` environment variable for `build.sh`, or the variable of
  the same name in the workflow);
- at run time: `--manifest <url>` or `DOOBLR_MANIFEST_URL`.

`make-release.py manifest --base-url URL` writes absolute links instead.

## Not tested yet

- Windows: the code builds only in CI and no CI run has happened yet.
- Signing in with a real GitHub token: tested only against a local mock of the GitHub API.
- The Mac administrator prompt (installing into `/Library`): the local tests install only into temp folders.
- The universal (x86_64) build; local builds are arm64.
