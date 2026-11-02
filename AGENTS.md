# AGENTS.md

## What this is

Wicky Chordinator is a JUCE 9 MIDI chord generator (VST3, Standalone, macOS
AU music effect), with audio/DSP skills in `skills/`. Audio passes through;
Velocity Gain changes outgoing MIDI velocity. `Source/` is the CMake/JUCE project.
The original brief and results are tracked under
`docs/prompts/build-juce-boilerplate/`. See `README.md` for user-facing documentation.

## Build and verify

- Release: `cmake --preset release` then `cmake --build --preset release`.
  Debug: the same with `--preset debug`. Presets use `build/<preset>` and cap
  jobs at 6.
- Without presets: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then
  `cmake --build build --config Release --parallel 6`.
- First configure downloads JUCE 9.0.4 with `FetchContent`. For offline or
  local builds add `-DBOILERPLATE_JUCE_SOURCE_DIR=/path/to/JUCE`.
- Artefacts land in
  `build/<preset>/JUCEBoilerplate_artefacts/<Release|Debug>/{Standalone,VST3,AU}/`.
  AU is macOS-only. Standalone suffix is `.app` on macOS, `.exe` on Windows,
  and empty on Linux.
- Enable tests with `-DBOILERPLATE_BUILD_TESTS=ON`, build all formats, then run
  `ctest --preset <preset>`. Use `xvfb-run -a ctest --preset <preset>` on headless
  Linux. Run effect and instrument tests; include Debug for implementation changes.
  Tests cover audio/state/layout behavior and editor lifecycle without audio devices.
- `instrument-release` / `instrument-debug` build an output-only, MIDI-enabled
  scaffold; no synthesis engine is supplied. Effect presets remain the default.
- `.github/workflows/build.yml` builds/tests both modes in Debug/Release on macOS,
  Windows, and Linux, and validates AUs on macOS. Release validation additionally
  requires pluginval and manual DAW/standalone checks (see `docs/distribution.md`).
- Out-of-memory during JUCE compilation: lower
  `-DBOILERPLATE_MAX_PARALLEL_JOBS=4` for Ninja; pass `--parallel 4` with other
  generators (presets request 6 jobs). LTO is off by default.
- C++17 is the default. Use `-DCMAKE_CXX_STANDARD=20` or
  `-DBOILERPLATE_WINDOWS_MIDI_SERVICES=ON` only when needed.
- Install plugins into JUCE's default folders with
  `-DBOILERPLATE_COPY_PLUGIN_AFTER_BUILD=ON`.

## Architecture rules

- `PluginProcessor` owns the APVTS and chord scheduler. Cache
  `getRawParameterValue()` pointers once and read the atomics in `processBlock`.
- Internal processing must stay free of allocation, locks, I/O, `juce::String`,
  `DBG()`, and `ValueTree` access. It uses `ScopedNoDenormals`; reset DSP state
  unconditionally in `prepareToPlay`.
  Keep the 64 KiB output reservation owned by the processor. The final copy into
  a host-owned MidiBuffer can allocate if the host supplies insufficient capacity;
  JUCE's API cannot guarantee that capacity. Do not claim the entire callback is
  allocation-free for arbitrary hosts. Overflow emits panic on all 16 channels.
  Merge pending and input events chronologically. Track generated and pass-through
  ownership independently and flush generated releases immediately on bypass.
- `PluginEditor` only lays out controls and uses APVTS attachments. No DSP.
- `Source/State/ParameterIDs.h` IDs, the AU version hint, and the four-character
  plugin/manufacturer codes are release contracts. Do not rename them after a
  release. Preserve each existing parameter's version hint. New parameters use
  a hint greater than every previously released hint; never bump all hints together.
  Hints preserve AU ordering, not semantic/state migration. Preserve released
  parameter ranges/meaning unless an explicit compatibility migration is designed.
- Instrument rendering belongs at the marked extension point in `processBlock`,
  after output clearing. Preallocate voices in preparation and honor MIDI sample
  offsets. Bypass/power disables chord generation and passes raw MIDI through.
- New parameter: ID -> layout entry -> cached raw pointer -> `processBlock` ->
  editor attachment.

## Validation

- AU on macOS: copy the built component to
  `~/Library/Audio/Plug-Ins/Components/`, run `killall AudioComponentRegistrar`,
  wait a few seconds, then `auval -v aumf Jbpl Wckm` for the effect or
  `auval -v aumu Jbpi Wckm` for the instrument presets. Rebranded codes must match.
- Launch the standalone with
  `open "build/release/JUCEBoilerplate_artefacts/Release/Standalone/Wicky Chordinator.app"`.
  If a screenshot is needed, capture the window with
  `screencapture -l <window-id>`. The `WickySnapshot` tool mentioned in
  `docs/prompts/build-juce-boilerplate/main-prompt.md` does not exist in this workspace.
- `.DS_Store` files are ignored; never commit them.

## Skills

- `skills/*/SKILL.md` are the project's audio-engineering standards, but they are
  not registered with OpenCode (no `opencode.json`), so the `skill` tool will not
  list them. Read the files directly.
- For any audio-code review, run `audio-dsp-review` (realtime safety) and
  `audio-numerics-review` (numeric correctness) before `juce-review`.
- Use `dsp-algorithm-guide` for new DSP implementations, `juce-guide` for plugin
  structure, and `audio-artifacts-debug` / `audio-performance-debug` for symptoms.
