## Build result

`cmake --build build/release --parallel 6` → all three targets built cleanly (Standalone, VST3, AU). No warnings from project code (the only message is Make's harmless jobserver unavailable note from JUCE's nested VST3-helper project).

### Verification performed

- Release build, all formats = pass
- Fresh-directory configure + build (`build/verify`) = pass
- `FetchContent` path with pinned JUCE 9.0.4 = pass
- Local-checkout path (`BOILERPLATE_JUCE_SOURCE_DIR`) = pass
- `-DBOILERPLATE_COPY_PLUGIN_AFTER_BUILD=ON` configure = pass
- `-DCMAKE_CXX_STANDARD=20` → `-std=c++20` in `compile_commands.json` = pass (default is c++17)
- `auval -v aufx Jbpl Wckm` = PASS (full validation, including render + parameter tests)
- State round-trip + unity-gain + bypass logic = pass (temporary harness, since removed)
- VST3 bundle signature = valid
- Standalone UI, visual review = verified via screenshot
- Tabs / stray whitespace in sources = none

### Files created

`CMakeLists.txt` presets, JUCE FetchContent 9.0.4, formats, OOM cap
`CMakePresets.json` debug/release, 6 jobs
`README.md` build, artefacts, params, architecture, signing
`AGENTS.md` condensed guidance for future sessions
`Source/PluginProcessor._` APVTS + DSP ownership, cached atomics, state, bypass param
`Source/PluginEditor._` layout + APVTS attachments only, resizable vector UI
`Source/DSP/GainProcessor._` SmoothedValue gain/bypass ramp, allocation-free
`Source/State/ParameterIDs.h` IDs + AU version hint
`Source/State/ParameterLayout._` gain (-60..+12 dB), bypass
`Source/UI/BoilerplateLookAndFeel.\*` vector knob, toggle, palette
`.gitignore` build artefacts, compile_commands, .DS_Store

#### Notes

- WickySnapshot does not exist in this workspace, so the UI was reviewed by launching the standalone and using `screencapture -l <window-id>` (screenshot above: dark panel, amber arc knob, `0.0 dB` readout, latching BYPASS toggle with LED, screw details, `VST3 / AU / STANDALONE` footer).
- State uses `ValueTree::writeToStream` / `readFromData` with a type check — the XML-based pair (`copyXmlToBinary` + `readFromData`) is mismatched and was proven broken by the test harness.
- `getBypassParameter()` returns the APVTS bypass so VST3/AU hosts see it as the canonical bypass.
- Nothing was committed: `git status` shows `.gitignore` modified plus untracked `Source/`, `CMakeLists.txt`, `CMakePresets.json`, `README.md`, `AGENTS.md`, `prompt.md`. `build/` (132 MB) is ignored.
