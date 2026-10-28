# AGENTS.md

## What this is

A JUCE 9 audio-effect boilerplate (VST3, Standalone, macOS AU) plus a library of
audio/DSP skills in `skills/`. `Source/` is a real CMake/JUCE project;
`prompt.md` is the untracked brief it was generated from. See `README.md` for
the full user-facing documentation.

## Build and verify

- Release: `cmake --preset release` then `cmake --build --preset release`.
  Debug: the same with `--preset debug`. Presets use `build/<preset>` and cap
  jobs at 6.
- Without presets: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then
  `cmake --build build --parallel 6`.
- First configure downloads JUCE 9.0.4 with `FetchContent`. For offline or
  local builds add `-DBOILERPLATE_JUCE_SOURCE_DIR=/path/to/JUCE`.
- Artefacts land in
  `build/<preset>/JUCEBoilerplate_artefacts/Release/{Standalone,VST3,AU}/`.
- There is no test suite or linter; a successful build of all formats is the
  verification step. AU can be validated with the commands below.
- Out-of-memory during JUCE compilation: lower
  `-DBOILERPLATE_MAX_PARALLEL_JOBS=4` or `--parallel`. LTO is off by default.
- C++17 is the default. Use `-DCMAKE_CXX_STANDARD=20` or
  `-DBOILERPLATE_WINDOWS_MIDI_SERVICES=ON` only when needed.
- Install plugins into the user folders with
  `-DBOILERPLATE_COPY_PLUGIN_AFTER_BUILD=ON`.

## Architecture rules

- `PluginProcessor` owns the APVTS and `GainProcessor`. Cache
  `getRawParameterValue()` pointers once and read the atomics in `processBlock`.
- `processBlock` must stay free of allocation, locks, I/O, `juce::String`,
  `DBG()`, and `ValueTree` access. It uses `ScopedNoDenormals`; reset DSP state
  unconditionally in `prepareToPlay`.
- `PluginEditor` only lays out controls and uses APVTS attachments. No DSP.
- `Source/State/ParameterIDs.h` IDs, the AU version hint, and the four-character
  plugin/manufacturer codes are release contracts. Do not rename them after a
  release. Bump `versionHint` only when an existing AU parameter changes meaning.
- New parameter: ID -> layout entry -> cached raw pointer -> `processBlock` ->
  editor attachment.

## Validation

- AU on macOS: copy the built component to
  `~/Library/Audio/Plug-Ins/Components/`, run `killall AudioComponentRegistrar`,
  wait a few seconds, then `auval -v aufx Jbpl Wckm`.
- Launch the standalone with
  `open "build/release/JUCEBoilerplate_artefacts/Release/Standalone/JUCE Boilerplate.app"`.
  If a screenshot is needed, capture the window with
  `screencapture -l <window-id>`. The `WickySnapshot` tool mentioned in
  `prompt.md` does not exist in this workspace.
- `.DS_Store` files are ignored; never commit them.

## Skills

- `skills/*/SKILL.md` are the project's audio-engineering standards, but they are
  not registered with OpenCode (no `opencode.json`), so the `skill` tool will not
  list them. Read the files directly.
- For any audio-code review, run `audio-dsp-review` (realtime safety) and
  `audio-numerics-review` (numeric correctness) before `juce-review`.
- Use `dsp-algorithm-guide` for new DSP implementations, `juce-guide` for plugin
  structure, and `audio-artifacts-debug` / `audio-performance-debug` for symptoms.
