# JUCE Boilerplate

A JUCE 9 audio-effect starter with a decoupled `Source/` layout, APVTS-backed
parameters, a realtime-safe DSP layer, and a resizable vector UI. One CMake
target builds VST3, Standalone, and (on macOS) AU.

## Requirements

- CMake 3.22 or newer, and a C++17 compiler.
- macOS: Xcode command line tools. Windows: Visual Studio 2022. Linux: the
  dependencies listed in the JUCE Linux prerequisites.
- The first configure downloads JUCE **9.0.4** with `FetchContent`. To build
  offline, point CMake at an existing JUCE 9 checkout (below).
- JUCE 9 is dual-licensed AGPLv3/commercial. This repository does not vendor
  JUCE source; you are responsible for a valid JUCE licence.

## Build

The presets use `build/debug` and `build/release`, and cap the build at 6 jobs:

```sh
cmake --preset release
cmake --build --preset release
```

Equivalent without presets:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 6
```

To use a local JUCE checkout instead of downloading it:

```sh
cmake --preset release -DBOILERPLATE_JUCE_SOURCE_DIR=/path/to/JUCE
cmake --build --preset release
```

Debug build:

```sh
cmake --preset debug
cmake --build --preset debug
```

### Build artefacts

With the release preset:

| Format | Path |
| --- | --- |
| Standalone | `build/release/JUCEBoilerplate_artefacts/Release/Standalone/JUCE Boilerplate.app` |
| VST3 | `build/release/JUCEBoilerplate_artefacts/Release/VST3/JUCE Boilerplate.vst3` |
| AU (macOS) | `build/release/JUCEBoilerplate_artefacts/Release/AU/JUCE Boilerplate.component` |

Plain `-B build` builds put the same tree under `build/`. Multi-config
generators (Xcode, Visual Studio) replace `Release` with the active config.

To install into the user plugin folders after each build:

```sh
cmake --preset release -DBOILERPLATE_COPY_PLUGIN_AFTER_BUILD=ON
cmake --build --preset release
```

Defaults are `~/Library/Audio/Plug-Ins/{Components,VST3}` on macOS and
`~/.vst3` on Linux.

## Parameters

| ID | Name | Range | Default |
| --- | --- | --- | --- |
| `gain` | Output Gain | -60 to +12 dB, 0.1 dB steps | 0 dB |
| `bypass` | Bypass | on/off | off |

The IDs in `Source/State/ParameterIDs.h` are the automation/preset contract.
Do not rename them after release. Bypass is smoothed to unity gain, so toggling
it during playback does not click.

## Architecture

```
Source/
  PluginProcessor.*        Owns APVTS + DSP; bus layout, state, thin processBlock
  PluginEditor.*           Layout and APVTS attachments only; no DSP
  DSP/GainProcessor.*      Smoothed gain/bypass, no allocation after prepare()
  State/ParameterIDs.h     Stable IDs and AU version hint
  State/ParameterLayout.*  Parameter definitions
  UI/BoilerplateLookAndFeel.*  Vector-drawn panel, knob, and switch
```

Data flow:

```
host -> processBlock -> GainProcessor -> output buffer
APVTS -> cached std::atomic<float>* -> processBlock   (audio thread)
editor controls -> APVTS attachments                  (message thread)
```

`processBlock` allocates nothing, takes no locks, performs no I/O, and touches
no JUCE `String`/`ValueTree` state. Keep it that way when extending the
boilerplate. The project skills in `skills/` document the relevant rules:
`audio-dsp-review`, `audio-numerics-review`, `juce-review`, and
`dsp-algorithm-guide`.

### Adding a parameter

1. Add the ID in `Source/State/ParameterIDs.h`.
2. Add the definition in `Source/State/ParameterLayout.cpp`.
3. Cache `apvts.getRawParameterValue(...)` in the processor constructor and read
   the pointer in `processBlock`.
4. Attach a control in `PluginEditor`.
5. Only bump `versionHint` if an existing AU parameter's meaning or range
   changes.

Use `-DCMAKE_CXX_STANDARD=20` only when required; `-DBOILERPLATE_WINDOWS_MIDI_SERVICES=ON`
forces C++20 for the Windows MIDI Services backend.

## Rebranding

Override these at configure time (or edit the defaults at the top of
`CMakeLists.txt`):

| Cache variable | Default |
| --- | --- |
| `BOILERPLATE_PRODUCT_NAME` | `JUCE Boilerplate` |
| `BOILERPLATE_COMPANY_NAME` | `Boilerplate Audio` |
| `BOILERPLATE_BUNDLE_ID` | `com.boilerplateaudio.juceboilerplate` |
| `BOILERPLATE_PLUGIN_MANUFACTURER_CODE` | `Wckm` |
| `BOILERPLATE_PLUGIN_CODE` | `Jbpl` |

Plugin and manufacturer codes must stay stable for the life of a released
product. Change them before the first release, not after.

For a universal macOS binary, add
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` when configuring.

## Signing and distribution

Local builds are ad-hoc/linker-signed and load in development hosts. Before
shipping, sign each bundle with your Developer ID:

```sh
codesign --force --sign "Developer ID Application: Your Name (TEAMID)" \
    "build/release/JUCEBoilerplate_artefacts/Release/AU/JUCE Boilerplate.component"
codesign --force --sign "Developer ID Application: Your Name (TEAMID)" \
    "build/release/JUCEBoilerplate_artefacts/Release/VST3/JUCE Boilerplate.vst3"
codesign --force --deep --sign "Developer ID Application: Your Name (TEAMID)" \
    "build/release/JUCEBoilerplate_artefacts/Release/Standalone/JUCE Boilerplate.app"
```

Notarize with `xcrun notarytool submit ... --wait` before distribution.

## Validate

Run the standalone app:

```sh
open "build/release/JUCEBoilerplate_artefacts/Release/Standalone/JUCE Boilerplate.app"
```

Validate the AU on macOS (the component must be installed first):

```sh
cp -R "build/release/JUCEBoilerplate_artefacts/Release/AU/JUCE Boilerplate.component" \
      ~/Library/Audio/Plug-Ins/Components/
killall AudioComponentRegistrar 2>/dev/null
sleep 2
auval -v aufx Jbpl Wckm
```

If the AU does not appear, restart the host and clear the AudioComponent cache
with `killall AudioComponentRegistrar`. `pluginval` is a good next check for
both VST3 and AU.

## Troubleshooting

- **Compiler killed / out of memory:** lower `BOILERPLATE_MAX_PARALLEL_JOBS`
  (default 6) or pass a smaller `--parallel` value. LTO is off by default; keep
  it off on low-memory machines.
- **Changed JUCE source path:** use a fresh build directory. CMake caches the
  JUCE location.
- **Plugin not listed in a DAW:** install it with
  `BOILERPLATE_COPY_PLUGIN_AFTER_BUILD=ON`, restart the host, and rescan
  plugins.
