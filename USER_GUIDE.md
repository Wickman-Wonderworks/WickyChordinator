# Wicky Chordinator — User Guide

Wicky Chordinator is a MIDI chord generator. Play one note and it plays a chord built from that note, using the chord recipe you choose. You can give individual keys their own recipes, so C4 and G4 (or even G3 and G4) can behave completely differently.

The audio path is a transparent pass-through: the plugin transforms MIDI, not audio. Put it before an instrument in your MIDI/effect chain.

## 1. Quick start

1. Load **Wicky Chordinator** on a MIDI or instrument track.
2. Route the plugin's MIDI output to the instrument you want to hear.
3. Choose a **Type**, **Complexity**, **Voicing**, and optional **Pitch Offset**.
4. Play a key. The original note is replaced by the chord rooted on that key.

The plugin is available as **VST3**, **AU** (macOS), and **Standalone**. In this build the AU is a Music Effect (`aumf`), so it appears as an audio effect with MIDI input rather than in Logic's dedicated MIDI FX slot.

## 2. Chord Settings

### Type
- **Major** — major thirds with major sevenths for the extended chords.
- **Minor** — minor thirds.
- **Dominant** — major thirds with flat sevenths.
- **Other** — the diminished family (diminished, diminished 7th, half-diminished, and their extensions).

### Complexity
Choose how much of the chord to play:

`Triad → 6th → 7th → add9 → 9th → 6over9 → 11th → 13th → 7b5 → 9#11 → 13#11`

`7b5`, `9#11`, and `13#11` are altered-dominant colours and use a flat seventh in every type.

### Voicing

**Voicing Style** in Global Settings selects how High / Root / Low work:

- **Register (legacy)** — moves the whole chord up an octave, leaves it in root
  position, or moves it down an octave.
- **Inversions** — High raises the bass; Root keeps root position; Low lowers
  the upper voice below the root, where the MIDI range permits.
- **Random** — chooses one of the three positions for each trigger.
- **Smart** — chooses a position/register to reduce movement from the previous
  chord on that MIDI channel.

New instances default to Inversions. Older saved sessions retain legacy register
voicing. Pitch Offset remains an independent semitone transpose.

### Pitch Offset
The `−` / `+` buttons transpose the chord from −12 to +12 semitones. This is useful for making a chord fit the key you are playing in, or for giving a single key a different version of the same chord.

Changing any matrix control affects the **next** chord you trigger. A chord that is already sounding keeps its notes.

## 3. Key Map — custom recipes per key

The bottom row of the panel is the key map.

1. Pick a note in the **KEY MAP** menu, e.g. `C4 (60)`.
2. Set the matrix above to the chord you want for that note.
3. Choose **All octaves** for a pitch-class assignment (all Cs, for example),
   or uncheck it to assign only the selected MIDI note. Click **ASSIGN**.
4. Play that key. It uses its recipe; every other unassigned key still follows the current matrix.

The status text shows what the selected key is using:

- **GLOBAL (uses matrix)** — no override; the key follows the matrix above.
- **CLASS …** — the selected pitch class has a saved recipe.
- **NOTE …** — the selected exact MIDI note has a saved recipe.
- **CLASS (inherited)** — the exact note uses its pitch-class recipe.
- **[NOTE overrides]** — an exact-note assignment takes precedence over the class.

### Editing an existing key recipe

1. Select the key in **KEY MAP**.
2. Click **LOAD**. Its saved recipe is copied into the matrix so you can see and edit it.
3. Change the matrix controls.
4. Click **ASSIGN** to save the changes back to that key.

### Removing a key override

Select the key and click **CLEAR**. With All octaves checked this clears the
pitch-class layer; otherwise it clears only the exact-note layer. Exact-note
assignments take precedence over pitch-class assignments, which take precedence
over the global matrix. Clearing an exact override may reveal an inherited class.

### Example

Uncheck **All octaves** for this example:

- `C4` → Major / Triad / Root / 0 → **ASSIGN**
- `G4` → Dominant / 13th / High / 0 → **ASSIGN**
- `C5` stays **GLOBAL** and follows whatever the matrix currently shows.

Exact-note assignments make `C4` and `C5` independent. All-octave assignments
share a recipe across the pitch class. **LOAD** edits whichever layer is selected.

## 4. Global interaction controls

These controls affect every chord, including keys with their own custom recipe.

### Spread

Sets how far a chord spans across the keyboard. At `0%` every chord uses its
close-position shape. Higher values open the voicing in octave steps.

### Spread Mode

- **Dense** — keeps every chord tone and pushes the upper voices outward
  gradually.
- **Sparse** — leaves the bass/root in place and lifts the remaining voices
  together, creating more space above the bass.

At `Spread = 0%` both modes are identical.

### Humanize

Randomizes velocity by up to 15 MIDI velocity steps, scaled by the amount.
With **Timing** enabled, it also adds up to 10 ms of random delay to each note-on.
At `0%` there is no humanization. Older sessions retain velocity-only behavior
until Timing is enabled. Velocity Gain still applies independently.

### Scale Root and Scale Mode

Scale Root transposes the result by 0–11 semitones. Modes are **Chromatic**,
**Major**, and **Natural Minor**. Natural Minor lowers the C-major reference
degrees E, A and B before root transposition; accidentals retain their chromatic
position. This transforms the preset rather than detecting the key automatically.
Green dots on the keyboard mark notes in the selected major/minor scale.

Scale choices are saved in DAW sessions and remain unchanged when loading factory
or user presets. Minor-mode NOW PLAYING shows output note names.

### Strum

Delays successive chord tones instead of firing them all at once. The step time
is calculated from the current sample rate, so the groove stays consistent at
`44.1`, `48`, and `96 kHz`. A `0%` setting keeps the original instant chord.

### Strum Mode

- **Up** — low chord tones first, high tones last.
- **Down** — high chord tones first, low tones last.

### Note Off Mode

- **Simultaneous** — every chord tone releases together when you let go of the
  key.
- **Staggered** — chord tones release one at a time. This is useful for short,
  arpeggio-like taps.

## 5. Power, velocity and presets

### Power

The large **ON / OFF** switch in the top bar arms or disables chord generation.
When it is **OFF**, input notes pass through unchanged, exactly like bypass.
The host-facing **Bypass** parameter still works independently.

### Velocity

The compact **Velocity** knob scales outgoing MIDI velocity.

- `0.0 dB` — the input velocity is used unchanged.
- Negative values soften the chord.
- `−60 dB` — the minimum note-on velocity (1).
- `+12 dB` — boosts velocity, clamped at 127.

### Presets

The **PRESET** selector in the top bar loads a factory or saved user preset. The `<` and `>`
buttons move through the bank without opening the menu. Loading a preset changes
the chord matrix, global interaction controls, power state and velocity; it does
not erase your mappings when loading a factory preset. A user preset restores
its saved exact-note and all-octave mappings. Both preserve the current scale.

### Chord readout

The large **NOW PLAYING** display shows the chord symbol currently sounding, for
example `C maj7`, `D m9`, or `G 13`. It uses the recipe assigned to the trigger
key, so custom key recipes are reflected immediately.

## 6. Note handling

- Triggering a key replaces the input note with the chord.
- Releasing the key releases the chord.
- Re-pressing a held key releases the old chord and starts the new one.
- If two held chords share a note, that note stays on until the last chord releases it.
- `CC123` (All Notes Off) and `CC120` (All Sound Off) release the chords on that channel.
- Channels are tracked independently.
- Very high or very low chords are moved by octaves so they stay inside the MIDI range.
- With **Strum** above zero, chord tones are scheduled across block boundaries; a
  very short key tap cancels any tones that have not sounded yet.
- With **Staggered** release, chord tones stop one at a time instead of together.

## 7. Saving

The chord matrix, the complete per-key map, the global interaction settings, power
state and velocity are saved with the plugin state and the DAW session. Reopening
the session restores every custom key assignment and interaction setting.

Click top-bar **SAVE** to write a named `.wickychord` preset. Presets in the default
user preset directory appear in the selector the next time the editor opens.
Click top-bar **LOAD** to open a preset file from another folder. This differs
from Key Map LOAD, which copies a selected mapping into the matrix.

The default folder is `Wickman Wonderworks/Wicky Chordinator/Presets` under JUCE's
user application-data directory (Application Support on macOS). User presets
include both mapping layers. DAW session recall also restores the selected scale.

## 8. Troubleshooting

**No sound**
- Check that the plugin's MIDI output is routed to an instrument.
- Check the instrument's MIDI channel.
- Check that **POWER** is ON and the host **Bypass** is off.
- In a DAW, make sure the track is record/monitor-enabled so MIDI reaches the plugin.

**A key does not follow the matrix**
- It probably has a custom recipe. Select it in **KEY MAP**; the status shows NOTE or CLASS. Clear the appropriate layer; an exact-note
  override wins over an all-octave assignment.

**A chord is in the wrong octave**
- Check **Voicing** (High/Root/Low) and **Pitch Offset**.

**Hanging notes**
- Stop the transport and start it again, or send All Notes Off from your DAW.
- Toggle **POWER** off and on, or toggle host **Bypass**, to force a release of generated notes.

**Standalone MIDI routing**
- Use the standalone's Options / Audio Settings to select MIDI inputs and an
  existing MIDI output device or port. This is JUCE's standard settings UI.
- This build does not create its own system-wide virtual MIDI output port.
  Routing to an existing virtual port depends on the OS/backend and your setup;
  creating a port and connecting to an existing port are separate capabilities.

## 9. Current limitations

These parts of the larger design are not in this build yet:

- Tempo-synced strum and additional strum patterns beyond Up / Down
- A custom standalone MIDI input/output settings panel (JUCE's standard panel exists)
- An app-created system-wide virtual MIDI output port
- Dedicated Logic MIDI FX placement through a validated bus-free AU MIDI effect

User-preset save/load and basic library browsing, Scale Root/Mode, inversions,
Smart/Random voicing, timing humanization, and all-octave assignments are already
implemented. The library currently offers save/load and default-folder discovery;
it does not provide tagging, search, or rename/delete controls.

The `BOILERPLATE_IS_MIDI_EFFECT` CMake option is scaffolding, not a completed Logic
MIDI FX configuration: the processor still declares audio buses. Do not enable it
and assume Logic compatibility without completing bus negotiation and validation.

See [the phased implementation plan](docs/plans/remaining-features.md) for the
remaining work, task boundaries, and acceptance checks.
