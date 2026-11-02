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
- **High** — the whole chord is voiced one octave above the played key.
- **Root** — the chord starts at the played key.
- **Low** — the whole chord is voiced one octave below the played key.

Voicing and Pitch Offset are independent: voicing moves in octaves, pitch offset moves in semitones.

### Pitch Offset
The `−` / `+` buttons transpose the chord from −12 to +12 semitones. This is useful for making a chord fit the key you are playing in, or for giving a single key a different version of the same chord.

Changing any matrix control affects the **next** chord you trigger. A chord that is already sounding keeps its notes.

## 3. Key Map — custom recipes per key

The bottom row of the panel is the key map.

1. Pick a note in the **KEY MAP** menu, e.g. `C4 (60)`.
2. Set the matrix above to the chord you want for that note.
3. Click **ASSIGN**. The key now has its own recipe.
4. Play that key. It uses its recipe; every other unassigned key still follows the current matrix.

The status text shows what the selected key is using:

- **GLOBAL (uses matrix)** — no override; the key follows the matrix above.
- **CUSTOM …** — the key has its own saved Type/Complexity/Voicing/Pitch.

### Editing an existing key recipe

1. Select the key in **KEY MAP**.
2. Click **LOAD**. Its saved recipe is copied into the matrix so you can see and edit it.
3. Change the matrix controls.
4. Click **ASSIGN** to save the changes back to that key.

### Removing a key override

Select the key and click **CLEAR**. The key returns to following the global matrix.

### Example

- `C4` → Major / Triad / Root / 0 → **ASSIGN**
- `G4` → Dominant / 13th / High / 0 → **ASSIGN**
- `C5` stays **GLOBAL** and follows whatever the matrix currently shows.

Each MIDI note has its own slot, so `C4` and `C5` are independent.

## 4. Velocity Gain and Bypass

The **MIDI Velocity Gain** knob scales the velocity of every generated chord note.

- `0.0 dB` — the input velocity is used unchanged.
- Negative values soften the chord.
- `−60 dB` — the minimum note-on velocity (1).
- `+12 dB` — boosts velocity, clamped at 127.

**BYPASS** stops chord generation and passes the input notes through unchanged. Any chord that is currently sounding is released when bypass is engaged, so you will not leave notes hanging.

## 5. Note handling

- Triggering a key replaces the input note with the chord.
- Releasing the key releases the chord.
- Re-pressing a held key releases the old chord and starts the new one.
- If two held chords share a note, that note stays on until the last chord releases it.
- `CC123` (All Notes Off) and `CC120` (All Sound Off) release the chords on that channel.
- Channels are tracked independently.
- Very high or very low chords are moved by octaves so they stay inside the MIDI range.

## 6. Saving

The chord matrix and the complete per-key map are saved with the plugin state and the DAW session. Reopening the session restores every custom key assignment.

A dedicated preset browser / user-preset library is not part of this build yet.

## 7. Troubleshooting

**No sound**
- Check that the plugin's MIDI output is routed to an instrument.
- Check the instrument's MIDI channel.
- Check that **BYPASS** is off.
- In a DAW, make sure the track is record/monitor-enabled so MIDI reaches the plugin.

**A key does not follow the matrix**
- It probably has a custom recipe. Select it in **KEY MAP**; the status will show **CUSTOM**. Click **CLEAR** to return it to the global matrix.

**A chord is in the wrong octave**
- Check **Voicing** (High/Root/Low) and **Pitch Offset**.

**Hanging notes**
- Stop the transport and start it again, or send All Notes Off from your DAW.
- Toggle **BYPASS** off and on to force a release of generated notes.

**Standalone MIDI routing**
- Use the standalone's Options / Audio Settings to choose your MIDI input.
- Feeding the standalone's generated MIDI into another app through a virtual MIDI port is not implemented in this build. Use the plugin inside a DAW for now.

## 8. Current limitations

These parts of the larger design are not in this build yet:

- Spread, Humanize, and Strum controls
- Strum patterns
- Preset browser and user-preset saving
- On-screen keyboard and octave-map display
- Scale / key-signature selection
- A custom MIDI input/output settings panel
- A system-wide virtual MIDI output port
- Dedicated Logic MIDI FX placement (requires the bus-free MIDI-effect build)

The chord matrix, per-key recipes, velocity gain, bypass, and session persistence are complete and active.
