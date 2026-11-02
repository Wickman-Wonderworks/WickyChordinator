# Wicky Chordinator

A JUCE 9 MIDI chord generator for macOS, Windows and Linux. Incoming notes become
chords, with per-note or all-octave assignments, inversions, voice leading,
spread, velocity/timing humanization, free or tempo-synced strumming and
staggered releases.

Audio passes through unchanged in the default configuration; the Velocity knob
scales generated MIDI velocities. Route MIDI output to an instrument to hear it.
The standalone does not contain a synthesizer.

![Project Screenshot](Assets/Images/interface-UI.png)

## Build

Requires CMake 3.22+, a C++17 compiler and JUCE 9.0.4. The first configure fetches
JUCE; use `-DBOILERPLATE_JUCE_SOURCE_DIR=/path/to/JUCE` for an existing checkout.
macOS requires Xcode command-line tools; Windows uses Visual Studio 2022.

```sh
cmake --preset release -DBOILERPLATE_BUILD_TESTS=ON
cmake --build --preset release
ctest --preset release
```

Repeat with `debug`, `instrument-release` and `instrument-debug`. Instrument
presets retain an output-only, silent synthesis extension point and separate
`JUCE Instrument` / `Jbpi` identity. The default product is the MIDI chord music
effect. Presets cap compilation at six jobs; use `--parallel 4` on low-memory
machines. LTO is off by default.

Linux dependencies (Ubuntu):

```sh
sudo apt-get install -y build-essential cmake pkg-config libasound2-dev libjack-jackd2-dev \
  libfontconfig1-dev libfreetype-dev libx11-dev libxcomposite-dev libxcursor-dev \
  libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libxi-dev xvfb xauth
```

Run tests under `xvfb-run -a ctest --preset release` on headless Linux.
Browser/curl support is disabled. JUCE licensing is the distributor's responsibility.

| Platform | Outputs                                  | Qualification                                             |
| -------- | ---------------------------------------- | --------------------------------------------------------- |
| macOS    | AUv2 music effect (`aumf`), VST3, `.app` | Universal arm64/x86_64 CI; local builds native by default |
| Windows  | VST3, `.exe`                             | Visual Studio 2022 x64 CI                                 |
| Linux    | VST3, executable                         | Ubuntu 22.04 x86_64 CI baseline                           |

CI builds/tests all four presets on these platforms, builds universal macOS binaries,
validates AUs on macOS, and uploads permission-preserving build archives. Tests run
on each runner's native architecture; Intel runtime/DAW testing remains a release check.
This is not a guarantee of compatibility with every OS version or DAW. Windows
and Linux ARM are outside the CI matrix. Qualify the oldest supported OS and
actual host/MIDI routing on each platform before release.

For universal macOS builds use a fresh build directory:

```sh
cmake -S . -B build/universal -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build/universal --parallel 6
```

Artifacts are under `build/release/JUCEBoilerplate_artefacts/Release/`:

- `Standalone/Wicky Chordinator.app` on macOS (`.exe` on Windows; no suffix on Linux).
- `VST3/Wicky Chordinator.vst3` on all platforms.
- `AU/Wicky Chordinator.component` on macOS.

`-DBOILERPLATE_COPY_PLUGIN_AFTER_BUILD=ON` installs plugins in JUCE's default
locations. Windows' system VST3 directory may require administrator privileges.
In standalone, use the window's **MIDI** button for input toggles, the output
endpoint, Preserve/1–16 channel routing, connection status and Send Note Offs.
The **Options** menu keeps the JUCE audio settings (audio input remains muted) and
state save/load. Standalone MIDI routing preferences are stored in the application
settings, not in plugin presets or DAW sessions. The standalone has no DAW
transport, so Sync strum timing uses the Fallback BPM control.

The default macOS AU remains a music effect (`aumf`). A separate
**Wicky Chordinator MIDI FX** AU (`aumi`, subtype `Wcmf`, manufacturer `Wckm`)
is available through the `midi-fx-debug` / `midi-fx-release` presets on macOS.
Its processor has no audio buses and shares the chord scheduler and state format.
Build with `cmake --preset midi-fx-release -DBOILERPLATE_BUILD_TESTS=ON`, then
`cmake --build --preset midi-fx-release` and `ctest --preset midi-fx-release`.
These presets build only AU; they preserve the existing effect/instrument identities.
Logic placement and project recall still require manual qualification.

On macOS and Linux, select **Wicky Chordinator Virtual Out** in standalone to
create an output other applications can receive. Each selected-route lifetime
gets a unique name suffix. Wicky virtual outputs are excluded from the app's
input list to prevent feedback. Windows uses existing physical/loopback endpoints;
app-created Windows ports are deferred. See [release qualification](docs/release-qualification.md)
for backend evidence and remaining platform checks.

## Playing and mapping

Choose a chord family, complexity, High/Root/Low voicing and pitch offset in the
matrix. Play a controller or click the on-screen keyboard. In Key Map, ASSIGN
copies the matrix to the selected key. With **All octaves** checked it applies to
the pitch class (all Cs, for example). Uncheck it for an exact MIDI note. Exact-note
assignments take precedence over pitch-class assignments. CLEAR affects the selected
layer; clearing an exact override reveals the pitch-class assignment beneath it.
LOAD copies that layer's recipe back into the global matrix.

Voicing Style controls how High/Root/Low are interpreted:

- **Register (legacy):** shift the whole chord +12/0/-12 semitones.
- **Inversions:** raise the bass for High, use root position for Root, or lower the
  top voice below the root for Low, keeping the same chord tones.
- **Random:** choose one of those three positions for each trigger.
- **Smart:** select a position/register minimizing movement from the previous
  chord on that MIDI channel. Reset/prepare clears this history.

Scale Root transposes the result by 0–11 semitones. Major retains the C-major
reference degrees; Natural Minor lowers E, A and B reference degrees by one
semitone before root transposition. Chromatic accidentals keep their chromatic
position. This is a deterministic transformation of the whole preset, not an
automatic chord-name/scale detector. The minor-mode readout shows output pitch
names. Scale Root/Mode persist in DAW sessions and stay unchanged when loading
factory or user presets.

Spread opens the upper voices by up to two octaves. Humanize varies velocity by
up to 15 MIDI steps; **Timing** additionally delays individual note-ons by up to
10 ms, scaled by the Humanize amount. This optional delay is an expressive effect,
not compensated audio latency. Strum has two timing modes: **Free** spaces notes
by up to 60 ms each with the Strum knob, while **Sync** uses the selected musical
division of the host tempo. If the host supplies no valid tempo (including the
standalone), the Fallback BPM value is used; the Global Settings status line says
whether the host tempo or fallback is active. **Strum Pattern** selects the finite
order of one chord: **Legacy Direction** follows Up/Down, **Alternate** alternates
Up and Down on successive triggers per MIDI channel, **Random** shuffles the chord
tones, and **Outside-In** plays lowest, highest, second-lowest, second-highest,
and so on. Non-legacy patterns override Up/Down. Tempo or pattern changes affect
future chords; tones already scheduled keep their absolute timing. Simultaneous
release cancels future tones; staggered release delays the releases of tones that
have sounded. Power OFF and host bypass flush generated notes immediately, then
pass raw MIDI through. CC120/123 flush the addressed channel only.

SAVE writes a `.wickychord` user preset containing parameters and both mapping
layers. The default preset directory is `Wickman Wonderworks/Wicky Chordinator/Presets`
under JUCE's user application-data directory (Application Support on macOS).
Presets saved there are listed on subsequent editor opens. LOAD opens preset
files from any folder. Green keyboard dots mark the selected scale. Factory presets update
global controls and retain mappings; user presets restore their saved mappings.
DAW session state always includes the complete configuration.

## Parameters and compatibility

| ID                            | Range / choices                                           | Default          |
| ----------------------------- | --------------------------------------------------------- | ---------------- |
| `gain`                        | Velocity Gain, -60 to +12 dB                              | 0 dB             |
| `bypass`, `power`             | Boolean                                                   | off, on          |
| `chord_type`                  | Major, Minor, Dominant, Other                             | Major            |
| `complexity`                  | Triad through 13#11 (11 choices)                          | Triad            |
| `voicing`                     | High, Root, Low                                           | Root             |
| `pitch_offset`                | -12 to +12 semitones                                      | 0                |
| `spread`, `humanize`, `strum` | 0–100%                                                    | 0%               |
| `spread_mode`                 | Dense, Sparse                                             | Dense            |
| `note_off_mode`               | Simultaneous, Staggered                                   | Simultaneous     |
| `strum_mode`                  | Up, Down                                                  | Up               |
| `strum_timing_mode`           | Free, Sync                                                | Free             |
| `strum_division`              | 1/8, 1/16, 1/32 plus dotted and triplet forms (9 choices) | 1/16             |
| `strum_fallback_bpm`          | 20–300 BPM                                                | 120 BPM          |
| `strum_pattern`               | Legacy Direction, Alternate, Random, Outside-In           | Legacy Direction |
| `voicing_style`               | Register, Inversions, Random, Smart                       | Inversions       |
| `humanize_timing`             | Boolean                                                   | on               |
| `scale_root`                  | C through B                                               | C                |
| `scale_mode`                  | Chromatic, Major, Natural Minor                           | Chromatic        |

Existing IDs, ranges, choice orders, AU version hints 1–16, and plugin codes are
preserved. New parameters use hints 17–20. States lacking the new parameters
explicitly restore legacy register voicing, velocity-only humanization, C
chromatic scale, Free / 1/16 / 120 BPM strum timing and Legacy Direction
ordering, preserving their prior sound. Old exact-note mappings remain exact-note
mappings. New state uses schema version 2.

## Architecture and realtime limits

`PluginProcessor` owns APVTS, cached parameter atomics, atomic recipe maps, MIDI
ownership and the scheduler. `PluginEditor` / `ChordSettingsPanel` handle controls,
presets and keyboard display on the message thread. `GainProcessor` remains a
separately tested utility; it is not in the chord processor's audio signal path.

The engine has 128 chord slots, 512 scheduled events, a 256-entry SPSC keyboard
queue and an owned 64 KiB output buffer. Scratch storage is reserved before
processing and never swapped with host storage. Incoming and scheduled events are
merged chronologically. Output expansion is bounded; output/input overflow clears
engine state and emits All Sound Off plus All Notes Off on every channel. The
input limit is 4096 host MIDI events per block. Keyboard queue overflow triggers
the same recovery. Pending-event exhaustion falls back to an immediate event.

Internal processing does not allocate, lock, perform I/O or access ValueTrees.
**JUCE's final copy into the host-owned MIDI buffer can allocate if that buffer
has insufficient capacity.** Hosts/wrappers must reserve enough output storage
for a strict allocation-free callback; the processor cannot guarantee this for an
arbitrary supplied MidiBuffer. Ordinary MIDI and SysEx share the output byte limit.

## Validation and distribution

Tests cover state migration/recall, legacy gain utility behavior, chord balance,
layouts, editor lifecycle, sample offsets, short/cross-block strums, bypass with
pending releases, multi-channel display, bounded output, inversions, scale,
humanize timing, user presets, tempo-synced timelines, host tempo/fallback
behavior, the timing controls, all finite strum patterns, standalone routing
settings/remapping and safe route switching. They do not open an audio device.

For local/CI AU validation, `bash scripts/validate-au.sh <preset>` applies an
ad-hoc development signature, installs the component into the user AU directory,
waits for registration and runs auval. This is separate from distribution signing.
An optional native integration check, `build/debug/BoilerplateTests --virtual-midi`,
creates two virtual ports and uses a second process to record and verify a processed
MIDI timeline. It requires OS MIDI-service access and is not part of CTest.

After installing the default AU, validate using:

```sh
auval -v aumf Jbpl Wckm
```

The instrument identity uses `auval -v aumu Jbpi Wckm`. Run pluginval strictness 5+
and actual DAW/standalone smoke tests on every release platform. Test both slices
of universal macOS binaries. See [distribution](docs/distribution.md) for signing,
notarization, installers and platform qualification.

Product identity is configurable in `CMakeLists.txt`: the defaults are Wicky
Chordinator / Wickman Wonderworks, bundle `com.wickyaudio.wickychordinator`,
manufacturer `Wckm` and plugin `Jbpl`. Never change released IDs or codes.
