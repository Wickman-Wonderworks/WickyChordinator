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

### Strum and Strum Timing

Strum delays successive chord tones instead of firing them all at once.
**Strum Timing** selects how the step is set:

- **Free** — the **Strum** knob sets the step from `0%` (instant chord) to `100%`
  (`60 ms` between tones). The step is calculated from the current sample rate, so
  the feel stays consistent at `44.1`, `48`, and `96 kHz`.
- **Sync** — the **Division** selector sets the full step as a musical value:
  `1/8`, `1/16` or `1/32`, each with straight, dotted and triplet forms. The step
  follows the host tempo. The Strum knob is disabled in Sync mode; its tooltip
  explains why.

**Fallback** is the tempo used when the host supplies no valid tempo. Its range is
`20–300 BPM` and the default is `120 BPM`. The status line at the bottom of Global
Settings shows `STRUM: HOST … BPM` when a host tempo is in use and
`STRUM: FALLBACK … BPM (NO HOST TEMPO)` when it is not. The standalone has no DAW
transport, so it normally uses the fallback rather than following a DAW clock.

Changing tempo or timing affects the **next** chord you trigger. Tones that are
already scheduled keep their absolute timing, so a tempo change never re-times a
strum that is already in progress.

### Strum Mode

Strum Mode is used by **Legacy Direction**. When another **Strum Pattern** is
selected it is disabled, and its tooltip explains that the pattern supplies the
order.

- **Up** — low chord tones first, high tones last.
- **Down** — high chord tones first, low tones last.

### Strum Pattern

Strum Pattern chooses the finite ordering of one chord. It is not a repeating
arpeggiator or sequencer.

- **Legacy Direction** — use the Up/Down Strum Mode above.
- **Alternate** — first trigger Up, next trigger Down, then repeat. Each MIDI
  channel keeps its own direction, so chords on different channels do not
  interfere.
- **Random** — shuffle all chord tones. Each tone appears exactly once per
  trigger.
- **Outside-In** — lowest, highest, second-lowest, second-highest, and so on.

Strum Pattern applies in both Free and Sync timing modes. It affects the next
chord you trigger; a chord that is already sounding keeps its scheduled order.

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

The chord matrix, the complete per-key map, the global interaction settings,
power state and velocity are saved with the plugin state and the DAW session.
This includes Strum Timing, Division, Fallback BPM and Strum Pattern. Reopening
the session restores every custom key assignment and interaction setting.

Standalone MIDI routing preferences (selected input identifiers, output endpoint
and Preserve/1–16 channel) are stored in the standalone application settings, not
in `.wickychord` presets or DAW sessions.

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

**The Strum knob is disabled**
- **Strum Timing** is set to **Sync**. The **Division** sets the full step in that
  mode. Switch Strum Timing to **Free** to use the knob.

**The Up/Down selector is disabled**
- A non-legacy **Strum Pattern** supplies the order. Choose **Legacy Direction**
  to use Up/Down.

**Sync strum is faster or slower than expected**
- Check the Global Settings status line. `STRUM: HOST … BPM` means the plugin is
  following a host tempo. `STRUM: FALLBACK … BPM (NO HOST TEMPO)` means the host
  supplied no tempo, so the **Fallback** value is in use. In the standalone there
  is no DAW transport, so set Fallback to the tempo you want.

**Standalone MIDI routing**
- Use the window's **MIDI** button to enable/disable input devices, choose an
  existing output endpoint, and select **Preserve** or a fixed output channel
  (`1–16`). **Refresh** re-scans the available endpoints.
- A selected endpoint that disappears is shown as disconnected. The plugin never
  substitutes another endpoint automatically; choose a new one or reconnect it.
- **Send Note Offs** releases held chord notes and panics the selected output.
- Switching the output endpoint flushes the old endpoint before opening the new
  one. Notes already sent to the old endpoint are released there; chord tones
  the scheduler has not emitted yet are generated on the new endpoint.
- **Audio Settings** in the Options menu keeps JUCE's audio device settings;
  audio input remains muted.
- This build does not create its own system-wide virtual MIDI output port.
  Routing to an existing virtual port depends on the OS/backend and your setup;
  creating a port and connecting to an existing port are separate capabilities.

## 9. Current limitations

These parts of the larger design are not in this build yet:

- An app-created system-wide virtual MIDI output port
- Dedicated Logic MIDI FX placement through a validated bus-free AU MIDI effect

User-preset save/load and basic library browsing, Scale Root/Mode, inversions,
Smart/Random voicing, timing humanization, tempo-synced strum with fallback BPM,
finite strum patterns (Alternate, Random, Outside-In), custom standalone MIDI
routing, and all-octave assignments are already implemented. The library currently
offers save/load and default-folder discovery; it does not provide tagging,
search, or rename/delete controls.

The `BOILERPLATE_IS_MIDI_EFFECT` CMake option is scaffolding, not a completed Logic
MIDI FX configuration: the processor still declares audio buses. Do not enable it
and assume Logic compatibility without completing bus negotiation and validation.

See [the phased implementation plan](docs/plans/remaining-features.md) for the
remaining work, task boundaries, and acceptance checks.
