# JUCE Boilerplate

A JUCE 9 audio-effect and software-instrument starter with a decoupled `Source/`
layout, APVTS-backed parameters, a realtime-safe DSP layer, and a resizable vector UI. One CMake
target builds VST3, Standalone, and (on macOS) AU. The default is a gain effect;
instrument presets provide an output-only, MIDI-enabled scaffold.

## Requirements

- CMake 3.22 or newer, and a C++17 compiler.
- macOS: Xcode command line tools. Windows: Visual Studio 2022. Linux: the
  development packages listed below.
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

Equivalent without presets (add `--config Release` when building with Visual Studio or Xcode):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 6
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

### Effect and instrument configurations

`release` / `debug` build the gain effect. `instrument-release` /
`instrument-debug` enable `BOILERPLATE_IS_SYNTH`, MIDI input, VST3 Instrument/Synth
categories, and an AU music device (`aumu`). Instruments have no audio input bus
and support mono/stereo output. They intentionally render silence until you add
an engine at the marked extension point in `processBlock`; render preallocated
voices there and honor MIDI event sample offsets. Bypass currently bypasses the
output gain stage, not the instrument's voice engine.

```sh
cmake --preset instrument-release
cmake --build --preset instrument-release
```

Instrument presets use a separate build directory, product name (`JUCE Instrument`),
bundle ID, and plugin code (`Jbpi`) so they can coexist with the effect (`Jbpl`).
When configuring manually, set `-DBOILERPLATE_IS_SYNTH=ON` and assign a distinct
product identity. Freeze effect/instrument classification before releasing a product.

### Platform targets and Linux dependencies

| Platform | Formats | Architecture / qualification |
| --- | --- | --- |
| macOS | AUv2, VST3, `.app` | Native build by default; arm64 + x86_64 universal option below |
| Windows | VST3, `.exe` | x64 with Visual Studio 2022; qualify Windows 10/11 hosts before release |
| Linux | VST3, executable | x86_64 Ubuntu 22.04 CI baseline; qualify other distributions separately |

CI builds both product configurations in Debug and Release on `macos-latest`,
`windows-2022`, and `ubuntu-22.04`. These build environments are not minimum runtime
OS guarantees. Set each product's minimum OS/architecture policy before release;
test on the oldest supported OS and actual DAWs. Windows/Linux ARM builds are not
covered by this CI matrix. Linux binaries inherit the build machine's libc baseline.

Ubuntu 22.04/24.04 dependencies for the enabled modules:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libasound2-dev libjack-jackd2-dev \
  libfontconfig1-dev libfreetype-dev libx11-dev libxcomposite-dev libxcursor-dev \
  libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libxi-dev xvfb xauth
```

Browser and curl support are disabled. Other distributions use different package
names; see the [pinned JUCE dependency list](https://github.com/juce-framework/JUCE/blob/9.0.4/docs/Linux%20Dependencies.md).

### Build artefacts

With the release preset:

| Format     | Path                                                                              |
| ---------- | --------------------------------------------------------------------------------- |
| Standalone | `build/release/JUCEBoilerplate_artefacts/Release/Standalone/JUCE Boilerplate.app` |
| VST3       | `build/release/JUCEBoilerplate_artefacts/Release/VST3/JUCE Boilerplate.vst3`      |
| AU (macOS) | `build/release/JUCEBoilerplate_artefacts/Release/AU/JUCE Boilerplate.component`   |

The table shows macOS bundles. Windows Standalone is `JUCE Boilerplate.exe`;
Linux Standalone is `JUCE Boilerplate` (no suffix). The VST3 bundle suffix remains
`.vst3` on all three platforms. Debug uses `build/debug/.../Debug/`; instrument
presets use `build/instrument-<config>/` and `JUCE Instrument` filenames.
Plain `-B build` builds put the same tree under `build/`. Multi-config
generators (Xcode, Visual Studio) use the configuration selected with `--config`.

To install into JUCE's plugin folders after each build:

```sh
cmake --preset release -DBOILERPLATE_COPY_PLUGIN_AFTER_BUILD=ON
cmake --build --preset release
```

Defaults are `~/Library/Audio/Plug-Ins/{Components,VST3}` on macOS and
`~/.vst3` on Linux. JUCE defaults to the system Common Files/VST3 directory on
Windows, which may require elevation. Override the plugin's `VST3_COPY_DIR` in
`juce_add_plugin` if you need a different development install location.

## Parameters

| ID       | Name        | Range                       | Default |
| -------- | ----------- | --------------------------- | ------- |
| `gain`   | Output Gain | -60 to +12 dB, 0.1 dB steps | 0 dB    |
| `bypass` | Bypass      | on/off                      | off     |

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
5. Give the new parameter its own version hint in `ParameterIDs.h`. After a
   release, each existing hint stays unchanged; new parameters use a hint greater
   than every previously released hint (first release: 1, next additions: 2, etc.).
   This preserves AU ordering in Logic/GarageBand. Do not remove released parameters.

A version hint is not a state-schema version and does not migrate changed parameter
semantics. Preserve released ranges/meaning, or design an explicit migration/new
parameter contract. See [JUCE's version-hint rules](https://docs.juce.com/master/classjuce_1_1AudioProcessorParameter.html).

Use `-DCMAKE_CXX_STANDARD=20` only when required; `-DBOILERPLATE_WINDOWS_MIDI_SERVICES=ON`
forces C++20 for the Windows MIDI Services backend.

## Rebranding

Override these at configure time (or edit the defaults at the top of
`CMakeLists.txt`):

| Cache variable                         | Default                                |
| -------------------------------------- | -------------------------------------- |
| `BOILERPLATE_PRODUCT_NAME`             | `JUCE Boilerplate`                     |
| `BOILERPLATE_COMPANY_NAME`             | `Boilerplate Audio`                    |
| `BOILERPLATE_BUNDLE_ID`                | `com.boilerplateaudio.juceboilerplate` |
| `BOILERPLATE_PLUGIN_MANUFACTURER_CODE` | `Wckm`                                 |
| `BOILERPLATE_PLUGIN_CODE`              | `Jbpl`                                 |

Plugin and manufacturer codes must stay stable for the life of a released
product. Change them before the first release, not after.

The editor title follows `BOILERPLATE_PRODUCT_NAME`; customize the remaining
subtitle/artwork for each product.

For a universal macOS binary, configure a fresh build directory with
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. Set an explicit minimum OS too, for
example `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0` for a product targeting macOS 11+,
and test both architectures. The default leaves these choices to the toolchain.

## Signing and distribution

Local macOS builds are ad-hoc/linker-signed. See
[Signing and distribution](docs/distribution.md) for Developer ID signing,
hardened runtime, notarization, installer packaging, and stapling. That guide also
covers Windows/Linux release checks. CI does not sign or publish releases.

## Validate

Build and run the behavioral tests (tests are opt-in):

```sh
cmake --preset release -DBOILERPLATE_BUILD_TESTS=ON
cmake --build --preset release
ctest --preset release
```

Repeat with `instrument-release`, and the Debug presets when changing code.
On a headless Linux machine use `xvfb-run -a ctest --preset release`. Tests cover
state recall, bypass at preparation, smoothing endpoints, gain extremes,
mono/stereo layouts, varying/empty blocks, silence, and editor construction,
painting, and destruction. They do not open an audio device.

Run the standalone app:

```sh
open "build/release/JUCEBoilerplate_artefacts/Release/Standalone/JUCE Boilerplate.app"
```

Validate the AU on macOS (the component must be installed first):

```sh
mkdir -p ~/Library/Audio/Plug-Ins/Components
cp -R "build/release/JUCEBoilerplate_artefacts/Release/AU/JUCE Boilerplate.component" \
      ~/Library/Audio/Plug-Ins/Components/
killall AudioComponentRegistrar 2>/dev/null
sleep 2
auval -v aufx Jbpl Wckm
```

For the instrument use the `instrument-release` component (`JUCE Instrument.component`)
and `auval -v aumu Jbpi Wckm`. Adjust codes if rebranding. macOS CI runs AU validation
for each configuration.

Release checks also include pluginval at strictness 5 or higher on VST3 (all
platforms) and AU (macOS), plus actual DAW and standalone smoke tests. Commands
and a checklist are in [the distribution guide](docs/distribution.md).

If the AU does not appear, restart the host and clear the AudioComponent cache
with `killall AudioComponentRegistrar`. `pluginval` is a good next check for
both VST3 and AU.

## Troubleshooting

- **Compiler killed / out of memory:** lower `BOILERPLATE_MAX_PARALLEL_JOBS`
  (default 6) for Ninja's compile pool. For Makefiles and Visual Studio, use
  `cmake --build --preset release --parallel 4` (the preset otherwise requests 6).
  LTO is off by default; keep it off on low-memory machines.
- **Changed JUCE source path:** use a fresh build directory. CMake caches the
  JUCE location.
- **Plugin not listed in a DAW:** install it with
  `BOILERPLATE_COPY_PLUGIN_AFTER_BUILD=ON`, restart the host, and rescan
  plugins.
