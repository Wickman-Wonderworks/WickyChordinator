# Remaining feature plan for Wicky Chordinator

## Purpose and baseline

This is an implementation handoff for a smaller coding model. Work on one numbered
unit at a time. Do not implement the entire document in a single turn. This plan
was checked against the repository on 2026-11-01; it is not a claim that new
features have been built or tested.

Read `AGENTS.md`, `README.md`, and `USER_GUIDE.md` first. Consult
`skills/juce-guide/SKILL.md` for structure and parameters, and
`skills/dsp-algorithm-guide/SKILL.md` when changing scheduling. Before completing
any audio-code change, apply `audio-dsp-review`, `audio-numerics-review`, then
`juce-review`, in that order. Apply `au-review` to the Logic format work.

### What already exists

| Feature | Current implementation | Work still needed |
| --- | --- | --- |
| User presets | SAVE/LOAD `.wickychord`, default-folder discovery, mappings in state | Optional library management UX only |
| Scale selection | Scale Root; Chromatic/Major/Natural Minor; session recall; presets preserve scale | Optional additional modes only |
| Strum | Free-time Up/Down, sample-clock scheduler | Tempo sync and additional finite patterns |
| MIDI settings | JUCE standard standalone input/output selector | Custom panel and verified routing lifecycle |
| Virtual output | Can select existing output endpoints through JUCE | Application-created port and platform capability handling |
| Logic MIDI FX | Default AU is `aumf`; MIDI-effect CMake switch exists | Separate bus-free product, zero-audio-bus processing, validation |

Evidence entry points: `Source/ChordSettingsPanel.h` (`PresetManager`,
`GlobalSettingsPanel`), `Source/PluginEditor.cpp` (`chooseUserPreset`),
`Source/State/ParameterLayout.cpp`, `Source/PluginProcessor.cpp` (`processBlock`,
`processPendingNotes`), and `Source/Standalone/StandaloneApp.cpp`.

## Rules for every implementation unit

- Preserve all existing parameter IDs, choice order, ranges, version hints, plugin
  codes, and legacy state migrations. Existing AU hints go through **16**. Allocate
  subsequent hints centrally and keep them unique across new work.
- Add separate timing/pattern parameters; do not reinterpret `strum` or reorder
  Up/Down. Free-time behavior and old sessions must sound unchanged.
- Keep device enumeration, file operations, UI updates, device opening/closing and
  OS MIDI calls outside `processBlock`. Any new cross-thread queues are bounded,
  preallocated, and have explicit overflow recovery.
- Preserve generated/pass-through ownership and immediate bypass flushing. Do not
  clear ownership silently while destination notes remain on.
- Retain internal output capacity. The final host-buffer copy still has the
  documented allocation caveat; do not claim universal allocation freedom.
- Keep device routing standalone-only. Plugin instances route through their host.
- Touch only the files needed for the current unit. If extraction is necessary,
  make it a behavior-preserving unit before adding the feature.
- End each unit with a short handoff: files changed, behavior, checks/results,
  unresolved platform checks, and the next unit. Do not report missing hardware
  or unavailable DAW checks as passes.

For code changes, configure tests with `-DBOILERPLATE_BUILD_TESTS=ON`, build all
formats and run CTest for `release`, `debug`, `instrument-release`, and
`instrument-debug`. Use the existing local JUCE path when available. Linux CI
uses Xvfb. Read cross-platform CI results before claiming cross-platform success.

## Phase 0 — Correct the documentation baseline

**Status: completed with this plan.** USER_GUIDE now identifies preset/scale
features already present, distinguishes standard MIDI settings from custom UI,
and explains the incomplete MIDI-effect option. No product code changes.

Do not rebuild presets or scale selection merely because an older document says
they are absent. Library search/rename/delete and additional scale modes are
optional enhancements outside the required phases below.

## Phase 1 — Tempo-synced strum

Dependencies: Phase 0. Keep this phase limited to timing; no new patterns or ports.

### Unit 1A — Timing calculation and parameters

Files: `Source/State/ParameterIDs.h`, `ParameterLayout.cpp`, new
`Source/DSP/StrumTiming.h` if useful, `Tests/BoilerplateTests.cpp`, and CMake only
if registering a new file.

1. Add `strum_timing_mode` (Free/Sync, default Free), `strum_division` (default
   sixteenth note), and `strum_fallback_bpm` (20–300, default 120). Use hints
   starting at 17; record exact assignments in ParameterIDs.
2. Define division choices explicitly in quarter-note beats: straight eighth
   0.5, sixteenth 0.25, thirty-second 0.125; dotted multiplies by 1.5 and triplet
   by 2/3. Freeze the shipped choice order.
3. Implement a pure bounded conversion:
   `stepSamples = round(sampleRate * 60 / bpm * quarterNoteBeats)`.
   Validate finite positive sample rate and BPM, guard integer conversion, and
   specify a maximum supported step duration. Use 120 BPM if host tempo is absent
   or invalid. The 20–300 range constrains the manual fallback, not valid host
   tempos; document any separate host bounds.
4. Free mode must retain the current `strum` percentage mapping exactly. Sync
   mode uses division as the full step; disable the free-time knob in that mode
   and explain why in its tooltip.

Accept: at 48 kHz/120 BPM, straight sixteenth is 6000 samples; eighth is 12000;
dotted sixteenth is 9000; triplet sixteenth is 4000. Test invalid/zero/NaN tempo,
multiple sample rates, defaults and old-state recall. No scheduler changes yet.

### Unit 1B — Host tempo and scheduler integration

Files: processor header/source and behavioral tests.

Read `getPlayHead()` / position information once per block without retaining host
pointers. Verify the pinned JUCE API locally. Use valid host BPM when available;
otherwise use the manual fallback, including in standalone.

Snapshot each chord's step when triggered. Already scheduled notes keep their
absolute due samples when tempo changes; future chords use the new tempo. This
avoids an unbounded rewrite of the pending queue. Do not add beat-grid start
quantization or a standalone transport in this unit.

Accept: compare complete absolute MIDI timelines at 64/512/4096-sample buffers;
verify tempo changes, tempo unavailable, rapid release, staggered release,
bypass/power, and pending-queue exhaustion. Keep manual playing possible when
host transport is stopped; changing transport position must not orphan notes.

### Unit 1C — Controls and documentation

Files: GlobalSettingsPanel, USER_GUIDE, README, editor tests.

Attach the timing selector, division selector, and fallback BPM control. Show when
fallback BPM applies; do not suggest the standalone follows a DAW clock. Verify
minimum editor size and session/preset recall. Explain the snapshot behavior.

Stop after all Phase 1 checks pass.

## Phase 2 — Additional finite strum patterns

Dependencies: Phase 1. This is a finite ordering of one chord, not a repeating
arpeggiator or sequencer.

### Unit 2A — Order generator

Add a pure fixed-array ordering helper and tests. Use a new `strum_pattern`
parameter: Legacy Direction (default), Alternate, Random, Outside-In. Legacy
Direction obeys existing Up/Down. New patterns override Up/Down explicitly; disable
that selector while overridden. Assign the next unused AU hint after Phase 1.

- Alternate: Up then Down on successive triggers, independently per channel.
- Random: a bounded Fisher–Yates permutation using the engine PRNG.
- Outside-In: lowest, highest, second-lowest, second-highest, etc.

Accept: for every chord size 1–8 each tone appears exactly once and indices remain
in range. Test deterministic random sequences with a test seed, channel-independent
Alternate state, and reset behavior. No heap containers or global RNG in rendering.

### Unit 2B — Engine/UI integration

Use the order helper with the existing sample scheduler. Do not introduce a second
scheduler. Keep bypass flushing, cancelled future tones, ownership and overflow
rules. Attach the selector, update docs and verify minimum UI layout.

Accept: old sessions reproduce Up/Down exactly; all patterns produce balanced
note-ons/offs across blocks and in both timing modes. Hold/release, retrigger,
shared tones and bypass remain covered. Ship after the common build/test gate.

## Phase 3 — Custom standalone MIDI settings

Dependencies: Phase 0; independent of Phases 1–2. Complete before creating ports.

### Unit 3A — Routing ownership and testable settings model

Files: standalone shell and new `Source/Standalone/MidiRouting.*`; CMake registration;
small device-backend abstraction and tests as needed.

Trace the pinned JUCE `StandalonePluginHolder` / `AudioProcessorPlayer` routing
before replacing anything. The standard selector already exposes inputs and an
output. Preserve that working path while defining a single owner for device
handles, output selection, persistence and panic.

Store stable device identifiers in standalone settings, not APVTS presets. Persist
selected input set, output endpoint, and output channel (Preserve/1–16). Define
missing-device behavior: show disconnected, never silently route somewhere else.

If channel remapping changes output, apply it consistently to note-ons, note-offs
and channel messages. Do not merge channels without accounting for shared-note
ownership. Prefer doing this at a dedicated, tested routing boundary.

Accept: fake-backend tests for enumeration, failed open, restart settings, missing
IDs, remapping and ownership. Merely enumerating a device is not a successful send.

### Unit 3B — Panel and safe route switching

Add a standalone settings button/panel with input toggles, output selector,
channel selector, refresh/status, and Send Note Offs. Preserve audio settings.
Do not add these hardware controls inside AU/VST3 editors.

Switch routes by releasing/flushing the old endpoint before closing it, establishing
a safe callback handoff, then selecting the new endpoint. Inspect JUCE's locking
and timing behavior rather than calling device methods from the processor.
If custom dispatch is required, use a bounded timestamped queue and non-audio
worker; preserve sample-offset timing and define panic recovery under saturation.
Never perform immediate bursts that discard strum offsets.

Accept: fake tests for switch while held/strumming, unplug/replug, shutdown,
repeated open/close, failure recovery and queue overflow. Manually verify actual
MIDI input/output and latency on macOS, Windows and Linux. Include a recorded
external MIDI timeline, not only a moving on-screen keyboard.

## Phase 4 — Application-created virtual MIDI output

Dependencies: Phase 3. Reuse its routing owner and dispatch path.

### Unit 4A — Capability investigation and macOS/Linux backend

Read the pinned JUCE endpoint implementation for each enabled backend. The local
MidiOutput header documents `createNewDevice` for Linux/macOS/iOS; JUCE 9's backend
architecture must still be checked, rather than inferring Windows support from
the presence of the method. Record a capability matrix and exact tested backends.

Add an opt-in standalone output `Wicky Chordinator Virtual Out` where supported.
Create/destroy endpoints outside rendering; own them for the app lifetime or
selected-route lifetime. Handle duplicate names/multiple instances, creation
failure and clean shutdown. Exclude the app's own endpoint from automatic input
selection so it cannot feed itself.

Accept: another process sees the endpoint and receives only the processed output,
with correct channels/timing. Test switching away, panic, app restart, multiple
instances and creation failure. Test real macOS CoreMIDI and Linux backends; a
mock verifies lifecycle but cannot prove OS-wide discoverability.

### Unit 4B — Windows capability and fallback

Determine whether the enabled Windows backend can publish a virtual endpoint on
the supported OS versions. Windows MIDI Services is an optional build choice,
currently off and requiring C++20 in this project; assess its deployment/runtime
requirements before adopting it. Do not implement or install a kernel driver as
part of this unit.

If the selected backend cannot create a port, show that capability accurately and
allow an existing externally created loopback port through Phase 3. Record this
as a fallback, not completion of app-created Windows virtual output. A genuinely
uniform app-created port requires a separately scoped backend/deployment decision.
Do not block the working VST3 and physical/existing-output paths on that decision.

Accept: compile/test both supported Windows configurations if enabling an alternate
backend; verify the failure UI and actual interprocess delivery on Windows.
Document which OS/backend combinations create a port versus use an external one.

## Phase 5 — Dedicated Logic MIDI FX product

Dependencies: Phase 0; independent of standalone routing and tempo sync.

### Unit 5A — Separate build identity and bus contract

Files: CMakeLists, CMakePresets, processor bus setup/layout, tests.

Introduce dedicated macOS MIDI-FX presets and a distinct product/bundle/plugin
code. Choose and document a valid unused four-character code; do not replace
released `Jbpl`/`Jbpi` identities. Reject incompatible synth + MIDI-effect flags.
Leave existing effect, instrument and standalone products unchanged.

For the new AU, enable `IS_MIDI_EFFECT`, declare zero audio buses, and accept the
correct empty layout. The existing CMake flag alone is insufficient. Prefer an
AU-only additional target/preset initially; do not accidentally convert the
standalone or all VST3 builds into a bus-free product.

Accept: baseline presets still pass; the new AU metadata declares `aumi`, has the
new codes and no audio buses, and round-trips all supported state.

### Unit 5B — MIDI-only timeline and host validation

Decouple scheduling's frame count from the assumption of a stereo buffer. Inspect
how the pinned JUCE AU wrapper supplies render-frame count for zero-channel
buffers. A zero-channel buffer is not necessarily a zero-frame render. If the
wrapper supplies no duration, stop here and design a narrow wrapper adaptation;
do not guess a constant block size or advance once per incoming note.

Accept: MIDI processing with zero channels and positive frame counts; delayed
note-ons/offs in blocks with no input MIDI; zero-frame handling; bypass/panic;
state/UI lifecycle; sample-rate and buffer-size changes. Verify absolute timelines
against the normal processor.

Install and run `auval -v aumi <new-code> Wckm` for Debug/Release; validate with
pluginval where supported. In Logic confirm dedicated MIDI FX placement, sound
through the following instrument, automation, saved project recall, strum tails,
power/bypass, transport changes and repeated editor opens. Compile success and
auval alone do not prove correct Logic slot placement or MIDI timing.

Update CI to build/validate the extra macOS presets without removing current ones.
Document the distinction between this AU and the existing music-effect AU.

## Phase 6 — Release qualification and handoff

Dependencies: completed features intended for that release; explicitly list any
Windows virtual-port work still deferred.

Run the common four-preset gate plus the new MIDI-FX presets, then cross-platform
CI, pluginval and actual standalone/DAW checks. Test native Apple Silicon and Intel
runtime, Windows x64, and the supported Linux baseline. Verify final packaged
artifacts, executable permissions, previous-session recall and signed builds per
`docs/distribution.md`. Do not change signing identities or publish a release as
an incidental consequence of implementation.

Record a compact feature/platform matrix with pass/fail/not-tested and evidence.
Update USER_GUIDE limitations only when each feature meets its acceptance gate.

## Suggested prompt for each smaller-model task

> Implement only Unit <number> in docs/plans/remaining-features.md. Read the
> baseline and rules in that plan and AGENTS.md. Inspect the named files before
> editing. Preserve released contracts and existing behavior. Add the specified
> behavioral tests, run the applicable checks, and report evidence and unresolved
> platform validation. Do not begin the next unit or claim untested device/DAW
> behavior works. If the pinned JUCE API cannot support the design, document the
> concrete blocker and the smallest alternative rather than inventing an API.
