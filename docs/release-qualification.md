# Remaining-feature implementation and qualification

Dates: 2026-11-01–02 (America/Phoenix). Local runtime: macOS, native arm64,
JUCE 9.0.4, C++17. Implementation and release qualification are separate.

## Implementation handoff

| Units | Implementation | Qualification still needed |
| --- | --- | --- |
| 1A–1C | Free/Sync parameters, bounded timing conversion, host/fallback tempo, scheduler integration and UI | Target-DAW automation/export checks |
| 2A–2B | Legacy, Alternate, Random and Outside-In through the same scheduler | Target-DAW playing/recall checks |
| 3A–3B | Standalone preferences, routing panel, remapping, endpoint switching, panic and reconnect handling | Physical MIDI input/output and latency on all three OSes |
| 4A | Opt-in selected-route virtual output on macOS/CoreMIDI and Linux/ALSA | Linux native delivery and manual routing/restart checks |
| 4B | Explicit Windows external-loopback fallback and unsupported native-port UI | Windows loopback delivery; native Windows output remains deferred |
| 5A–5B | Separate AU-only product, zero-audio-bus processor, MIDI-only frame timeline, presets and CI jobs | Actual Logic placement/project/automation checks |
| 6 | Local build/test/validator evidence and this matrix | Full release gate remains open; no release published or distribution identity changed |

The new product is **Wicky Chordinator MIDI FX**, bundle
`com.wickyaudio.wickychordinator.midifx`, AU `aumi Wcmf Wckm`.
`Wcmf` is distinct from every pre-existing product code in this repository;
`Jbpl`/`Jbpi`, parameter IDs, ranges, order and AU hints 1–20 are unchanged.
State remains compatible across products; old sessions still default to Free
and Legacy Direction. MIDI FX is AU-only and cannot combine with synth mode or
reuse either released effect/instrument code. Invalid combinations are checked
at CMake configure time.

### Pinned wrapper investigation

JUCE's `juce_audio_plugin_client_AU_1.mm`, `Render`, calls
`audioBuffer.getBuffer(nFrames)` even when the processor has zero audio channels.
`juce_AU_Shared.h`, `CoreAudioBufferList::getBuffer`, sets the buffer's frame count
independently of channel count. No wrapper patch or guessed clock is needed.
JUCE's `getBusCountForWrapper` supplies a host-facing compatibility output for
MIDI processors; AU host tools may consequently display a stereo output. The
product's **AudioProcessor has zero audio buses** and renders MIDI with the exact
host frame count. The stock wrapper convention is retained rather than removing
its compatibility output. The literal zero-bus requirement at the AU wrapper
boundary therefore remains a documented difference from the original plan.
Logic slot placement has not been tested and must not be inferred from auval.

### Virtual-output backend investigation

| OS/backend | App-created output | Evidence |
| --- | --- | --- |
| macOS/CoreMIDI | Implemented; creation can fail and is reported disconnected | Native interprocess timeline check passed on arm64 |
| Linux/ALSA | Implemented; needs a working ALSA sequencer | Pinned backend inspected; native OS delivery not tested here |
| Windows/WinMM (default) | Unsupported; external loopback fallback | Pinned native session does not override virtual creation; default implementation returns null |
| Windows MIDI Services (optional, off) | Deferred by product policy | Pinned JUCE implementation has virtual-endpoint creation; C++20, SDK/service deployment and supported OS/runtime requirements still need a separate Windows decision |

Source evidence in pinned JUCE: `midi_io/juce_MidiDevices.cpp`,
`MidiOutput::createNewDevice` -> `Session::createLegacyVirtualOutput`;
`native/juce_CoreMidi_mac.mm`, `native/juce_Midi_linux.cpp`,
`native/juce_Midi_windows.cpp` and `midi_io/ump/juce_UMPSession.cpp`.
No driver or alternate Windows service was installed or enabled.

A stable preference value `wicky:virtual-output` recreates the selected virtual
route on restart. Every creation gets a unique suffix to distinguish instances.
Closing/switching the route detaches the player, clears queued output, panics the
old endpoint and destroys it before opening the replacement. Native handles and
enumeration remain outside rendering. The existing JUCE timestamped output worker
is reused. Wicky virtual sources are excluded from input selection/automatic
opening to avoid feedback, including sources from other Wicky instances.
Receivers must select the recreated suffix after route recreation/restart.

### Review fixes

- Large SysEx bypasses owning `MidiMessage` construction in the routing callback;
  raw bytes go to its reserved bounded output buffer. Malformed note data is
  checked before indexing note-ownership arrays.
- Disabling or detecting loss of an input requests global cancellation. The
  proxy injects CC120 before ordinary input through reserved storage, cancelling
  processor-scheduled future tones and releasing output ownership. This affects
  chords from all inputs; input identity is not retained inside the chord engine.
- Send Note Offs uses that same atomic request, replacing the collector path with
  zero timestamps. Zero-frame callbacks retain pending requests. Saturated panic
  input is dropped as a whole, avoiding a prefix with unmatched note-ons. Remap
  overflow emits panic and requests processor cancellation for the next block.
- Output-channel changes release old ownership before applying the new policy.
- The declared tail is a constant 70.01 seconds, covering seven maximum synced
  steps and humanization. This conservative bound can extend host export tails.
- Output clearing checks the actual buffer channel count, allowing zero-channel
  timelines in the instrument scaffold as well as MIDI FX.
- AU validation seals bundles with an ad-hoc development signature and polls for
  registration. Distribution signing remains a separate qualification gate.

## Local verification

The common gate builds all formats and runs both CTest tests for `debug`,
`release`, `instrument-debug`, `instrument-release`, `midi-fx-debug` and
`midi-fx-release`, with `BOILERPLATE_BUILD_TESTS=ON` and local JUCE 9.0.4.
Final gate: all six presets built successfully and all 12 CTest checks passed.
Both invalid CMake configurations (synth+MIDI FX and released MIDI FX identity)
were rejected as expected.

| Check | Result |
| --- | --- |
| All six preset builds | Pass |
| All six CTest presets (12 checks) | Pass |
| Final effect Debug/Release auval | Pass |
| Final instrument Debug/Release auval | Pass |
| Final MIDI FX Debug auval | Pass |
| MIDI FX Release auval | Earlier build passed; final reinstallation/rerun was not approved and remains pending |
| Final effect AU/VST3 pluginval, strictness 5 | Pass |
| Final instrument AU/VST3 pluginval, strictness 5 | Pass |
| Final MIDI FX Debug AU pluginval, strictness 5 | Pass |
| MIDI FX Release AU pluginval, strictness 5 | Earlier build passed; final Release validation remains pending |
| Final native CoreMIDI interprocess timeline | Pass; [recorded events](validation/native-midi-timeline.csv) |
| Whitespace/diff checks | Pass |

The validation sequence left **MIDI FX Debug** installed. The final Release
reinstallation was rejected by the permission prompt and was not retried.
Earlier Release auval/pluginval evidence is retained separately and is not
presented as validation of the final rebuilt Release bundle.
Local build/test/validator logs are retained under `build/validation/` (ignored
build artifacts); the recorded native timeline is versioned with this report.

New regression coverage compares complete MIDI timelines with zero versus two
buffer channels at 44.1/48/96 kHz and 64/512/4096-frame blocks, including delayed
releases and blocks without input. It also checks empty layouts, invalid audio
layouts, zero frames, bypass, panic cancellation, saturated input, SysEx bytes and
offsets, virtual preference recall, creation failure, recovery and close/panic.
Existing tests retain state/UI lifecycle, sample-rate and block-size coverage.

The optional `BoilerplateTests --virtual-midi` integration check opens no audio
device. It creates two native ports with distinct names, launches a second process
to discover/open one input, sends a processed chord through the routing proxy and
JUCE's scheduled output thread, and verifies six note messages on channel 7.
Expected relative times are 0, 60, 120, 250, 310 and 370 ms (30 ms tolerance).
The recorded final run differed from target spacing by less than 0.02 ms.
This proves native CoreMIDI interprocess delivery; it does not prove hardware
latency, audio-device lifecycle or DAW behavior.

Apple validation: `bash scripts/validate-au.sh <preset>` installs the selected
component in the user AU folder and runs `auval` after development signing.
Pluginval: official Tracktion pluginval v1.0.4, strictness 5, native arm64.
Neither validator establishes actual Logic slot placement or project behavior.

### Cross-platform CI

The previous pushed commit `e263fde` was checked in
[run 41399487183](https://github.com/Wickman-Wonderworks/WickyChordinator/actions/runs/41399487183).
All four Windows and all four Linux build/test jobs passed. All four macOS jobs
built and passed CTest, then failed AU discovery with “didn't find the component”.
Those historical passes do **not** validate these uncommitted changes. CI now
includes both macOS MIDI FX presets and uses the shared development-signing and
registration-wait procedure. A fresh run must pass after these changes are pushed.

## Code-review handoff

The audio-dsp, audio-numerics, JUCE and AU reviews were applied in that order.
New callback work uses fixed-capacity storage, bounded loops, cached atomics and
raw system-message copies; device/OS/file work remains on the message thread.
Note-index validation and bounded timing avoid integer indexing/conversion errors
for supported inputs. Preparation resets runtime state. No new internal render
allocation, locking or I/O was found in this final pass. The documented final
copy into a host-owned MidiBuffer may allocate, and JUCE's standalone player and
MIDI-output worker retain their framework locks/queues; this is not a claim that
an arbitrary host's whole callback is allocation-free.
The AU wrapper's compatibility output and manual Logic gate remain as described
above. These reviews do not substitute for platform/runtime qualification.

## Remaining release gates

- Logic: MIDI FX slot before an instrument, audible following instrument,
  automation, saved-project recall, strum/release tails, power/bypass, transport
  changes and repeated editor opens. Logic is not installed on the local machine.
- Physical MIDI: recorded external timelines, input unplug/replug while held,
  output switching during a strum, latency, repeated open/close, shutdown and
  audio-device changes on macOS, Windows and Linux.
- Linux ALSA virtual discoverability/delivery and multi-instance/restart behavior;
  Windows real loopback delivery/failure UI. App-created Windows ports deferred.
- Fresh cross-platform CI, native Intel runtime and supported Linux/Windows
  baseline tests. Local binaries are arm64, not universal release packages.
- Final Developer ID/Windows signing, notarization, installer permissions and
  clean-machine previous-release recall per `distribution.md`. No credentials,
  distribution identities, releases or published artifacts were changed.

Next action: finish the outstanding platform/manual gates using the matrix;
Phase 6 must remain open until their evidence exists.
