# dooblr shared preset paths

`PresetPaths.{h,cpp}` is the suite helper for **user preset folders**. Every JUCE plug-in
that saves a user preset (or lists them in the header combo) goes through it.

| Platform | User presets | Factory files (if an installer ever ships them) |
|---|---|---|
| macOS | `~/Library/Audio/Presets/dooblr/<plugin>/` | `/Library/Audio/Presets/dooblr/<plugin>/` |
| Windows | `%USERPROFILE%\Documents\VST3 Presets\dooblr\<plugin>\` | `%PROGRAMDATA%\VST3 Presets\dooblr\<plugin>\` |
| Linux | `~/.vst3/presets/dooblr/<plugin>/` | `/usr/share/vst3/presets/dooblr/<plugin>/` |

Factory patches in this suite are compiled into each plug-in. The helper still exposes the
factory folders so an installer can drop `.vstpreset` (or the plug-in's own format) there later.

Until this helper, user presets were written to `~/Music/<plugin>/Presets`. On first access
those files are **copied** into the new folder (not moved or deleted). The combo still lists
any leftover unique names from the old folder.

Non-preset data is out of scope: dooblr-generate history and model settings stay in
`~/Library/Application Support/dooblr/generate/` (never read `hf_token` from here). Kick's
WAV export folder stays `~/Music/dooblr-kick`.

dooblr-rhythm (open PR, branch `cursor/dooblr-rhythm-plugin-3f0c`) still uses the Music path
on that branch. Rebase it onto this helper the same way as bass / prophet.

## Tests

```sh
# from the repo root; uses ~/dooblr-build/_deps/juce-src when it exists
cmake -S common -B ~/dooblr-build/dooblr-preset-paths \
  -DCMAKE_BUILD_TYPE=Release \
  ${JUCE_SOURCE_DIR:+-DFETCHCONTENT_SOURCE_DIR_JUCE="$JUCE_SOURCE_DIR"}
cmake --build ~/dooblr-build/dooblr-preset-paths --config Release
~/dooblr-build/dooblr-preset-paths/DooblrPresetPathTests_artefacts/Release/DooblrPresetPathTests
```

`DOOBLR_PRESET_HOME` / `DOOBLR_PRESET_MUSIC` redirect the native folders so the tests never
touch the real Music or Library directories.
