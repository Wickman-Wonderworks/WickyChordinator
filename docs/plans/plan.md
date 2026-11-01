# Plan for Wicky Chordinator

## Key Features

- Universal Compatibility: Effortlessly connect with your DAW and MIDI controller for a smooth and hassle-free setup.
- Chord Generation: From simple triads to complex jazz chords, create stunning chord progressions with ease.
- Smart Voicing: Intelligently selects the most pleasing chord voicings, ensuring your music always sounds professional.
- Spread, Humanize and Strum: Add depth and realism to your chords with adjustable spread, humanization and strumming effects.
- Strum Patterns: Choose from a variety of strum patterns to suit any genre or style, and bring your compositions to life.
- Key Signature: Easily play in any key signature, the perfect companion for songwriters and composers.

## MIDI Settings

Click the gear icon to enable/disable different MIDI inputs. By default all MIDI inputs are active. If you have multiple controllers and only want some to run through Chordable, you can deactivate them here by un-checking the Input box. Select "Input" if you want the controller to feed into Chordable and go to your DAW. Select "Output" if you want to send output from Chordable out to your MIDI controller. You can specify the MIDI Output Channel here as well, although typically you won't need to change this. If you do encounter hanging notes, you can use the "Send Note Offs" button to mute all notes.

## Preset Selection

Select a preset by clicking on this bar. Alternately, you can use the arrow buttons to navigate through the list of presets.

## Preset Menu

Clicking on the preset bar will open up this menu to select a preset. If you have Chordable Pro, you'll see your custom user presets on the right hand side.

Some presets may appear locked if you are using the free version of Chordable. If you have Chordable Pro, all presets will be available and you will have the ability to save your own custom presets.

## Save Preset

If you have Chordable Pro, you'll be able to save your own custom presets. This icon will become active whenever you change a default value from a preset. A dialogue will open up for you to specify the name of your saved preset.

## Scale Settings

These dropdowns set the root and mode of the current preset (i.e. the scale). If you change these values, it will transpose your current preset into the selected scale. When you change these values, you will see the gray keys in the first octave located in the bottom left-hand corner update to reflect the notes that are contained in the new scale, to serve as a guide so that you know which keys are in the scale.

These values are NOT part of the preset. So, if you have these set to D minor, for instance, loading a preset will not change those values; you'll stay in the D minor. This enables you to experiment with lots of presets while staying in your desired key.

**Note:** if you have a preset that uses the "pitch offset" feature, the "Root" and "Mode" parameters may be less meaningful, but can still be used to transpose your preset around the keyboard.

## Effect Bypass

If you turn this off, you will just hear the input notes as you play them. This can be useful if you are wanting to switch between recording melodies and chords.

## Chord-specific Controls

Changing these values only affects the chord associated with the current note that you are playing.

## Chord Type

This sets the type of chord associated with the currently selected note. Changing this selection will update the "Complexity" section with the different types of chords within the selected type of chord. Major and Minor chords are most commonly used in pop music. Dominant chords are more common in Jazz, and more exotic chords that are not part of any of the above are found in the "Other" category.

## Chord Complexity

For each chord type, there is a list of different complexities. Triads are the most basic chords, and the chords become more complex as more notes are added. You don't need to worry about the names if they are unfamiliar to you, they are listed generally in order of increasing complexity. 13th chords, for instance, are most commonly used in Jazz music. 7th chords, on the other hand, are now commonly found in pop music, lofi, hip hop, etc.

## Voicing

Select whether to use an "inversion" voicing. Inversions are basically when the notes in a chord are re-ordered so that the lowest note is not the root note. Skilled piano players often use inversions and can do so on the fly.

If you have Chordable Pro, you have access to the "Random" (?) and "Smart" (💡) controls, which can replicate how experienced piano players play. Random selects an inversion at random, Smart selects an inversion based on the previous chord played. Selecting Random or Smart will override the individual voicing settings for all chords.

## Pitch Offset

Use this control to shift the currently selected chord up or down by half-steps (only applies to current chord). This enables you to map multiple G chords within a preset, for instance.

## Global Controls

Changing these values will affect all chords.

## Spread

Sets how far the chord should span across the keyboard. A low value here will result in the minimum number of notes needed for the chord, within a ~1 octave span. Increasing this value will spread the notes over a larger span, based on the spread mode selected.

## Spread Mode

The left option (Dense) will result in dense chord voicings with many notes. The right option (Sparse) will result in sparser voicings with more space between the bass note and the rest of the chord. At Spread = 0, both modes are the same.

## Humanize

This controls randomizes the velocity and timing of the notes, making it sound more realistic. At Humanize = 0, the velocity of the chord notes will be equal to the input note, and will be evenly spaced in timing.

## Note Off Mode

With Simultaneous Release (left option), all chord notes will mute when you release the note. With Staggered Release (right option), the chord notes will mute one at a time. This option is most useful for quick arpeggios where you tap the note briefly. With Simultaneous Release, if you tap the note, the subsequent notes won't be triggered.

## Strum

Adjusts the timing in between chord notes. Tempo sync coming soon!

## Strum Mode

Chordable Pro feature — sets the order of notes that are played in the chord. The ? will select a Strum Mode at random. The 💡 will enable Smart Strum mode, which alternates between Strum Up and Strum Down based on the previous chord.

## Octave Map

The first octave in the bottom left corner shows you which note you are currently mapping. In the example here, you are currently mapping ALL C notes on the keyboard. The gray notes show you which notes are in the scale as defined in the upper right corner by the Root and Mode. This serves as a guide to help you know which notes are in the scale. If your preset is using the "Pitch Offset" feature, this guide may be less applicable.

## Keyboard

You can click on keys here to trigger chords, if you don't have a MIDI controller.
