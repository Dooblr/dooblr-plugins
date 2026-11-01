# dooblr installer

One app for macOS and Windows that installs and updates the whole dooblr suite. It shows every
plug-in with the version you have and the latest version, and you tick the ones to install or
update.

| Item | Kind | Goes into |
| --- | --- | --- |
| dooblr-q, dooblr-kick, dooblr-vocals, dooblr-prophet, dooblr-soothe, dooblr-feedback | VST3 | Mac: `/Library/Audio/Plug-Ins/VST3` (asks for your password once), or `~/Library/Audio/Plug-Ins/VST3` (just you).<br>Windows: `C:\Program Files\Common Files\VST3` (asks for permission, UAC), or `%LOCALAPPDATA%\Programs\Common\VST3` |
| dooblr-prophet panel (optional), dooblr-chords | Max for Live device (Ableton Live only) | `<Ableton User Library>/Presets/MIDI Effects/Max MIDI Effect/<device>`, the same folder the dooblr-chords and dooblr-prophet build scripts use, so Live's browser shows them under MIDI Effects > Max MIDI Effect |

Version 0.1.0.

## Download

- **macOS:** [dooblr-installer-mac.dmg](https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-installer-mac.dmg)
- **Windows:** [dooblr-installer-windows.exe](https://github.com/Dooblr/dooblr-plugins/releases/latest/download/dooblr-installer-windows.exe)

These links work once a release has been published (see [Releasing](#releasing)). **The repository is
private right now**, so the links (and the installer's own download of the manifest) only work for
people signed in to GitHub with access. For public downloads, make the repository public, or host the
release files somewhere public and change the manifest address (see [Changing the download address](#changing-the-download-address)).

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
```

Environment: `DOOBLR_MANIFEST_URL`, `DOOBLR_VST3_DIR`, `DOOBLR_M4L_DIR`, `DOOBLR_INSTALLER_RECEIPTS`.
Exit codes: 0 ok, 1 something failed, 2 bad arguments, 3 manifest could not be read.

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

`scripts/e2e-local.sh` (Mac) packages the plug-ins you have built in `~/dooblr-build`, lists them
against your real folders (read only), installs everything into a temp folder, checks the
signatures, and checks that a corrupted zip is rejected.

## Releasing

Releases are built by GitHub Actions (`.github/workflows/release.yml`):

```sh
git tag v0.1.0 && git push origin v0.1.0
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
- The Mac administrator prompt (installing into `/Library`): the local tests install only into temp folders.
- The universal (x86_64) build; local builds are arm64.
