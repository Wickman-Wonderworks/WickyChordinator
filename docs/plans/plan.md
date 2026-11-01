# Build Wicky Chordinator

You are an expert audio software engineer working in JUCE 9 and C++. I am building a chord progression plugin. I have an existing, working boilerplate project, and I need you to integrate a new "Chord Settings Matrix" feature across my existing files.

Do not rewrite my entire project or invent generic placeholder functions. Instead, examine the existing structures below and generate exact, targeted additions or inline code replacements. Ensure that all parameter lookups on the audio thread are 100% lock-free, allocation-free, and realtime-safe.

Here is the exact code for my existing files:

- Source/PluginProcessor.h (file exists)
- Source/PluginProcessor.cpp (file exists)
- Source/ChordSettingsPanel.h (create this file)

### Feature Requirements to Inject:

1. Parameter Layout: Add three discrete `juce::AudioParameterChoice` parameters to my APVTS layout:
   - "chord_type" ("Chord Type"): Choices are Major, Minor, Dominant, Other. (Default index 0)
   - "complexity" ("Complexity"): Choices are Triad, 6th, 7th, add9, 9th, 6over9, 11th, 13th, 7b5, 9#11, 13#11. (Default index 0)
   - "voicing" ("Voicing"): Choices are High, Root, Low. (Default index 1)

2. High-Performance Caching: Declare three `std::atomic<float>*` pointers (`typeParam`, `complexityParam`, `voicingParam`) in the private section of `PluginProcessor.h`. Cache them using `apvts.getRawParameterValue` inside the processor's constructor immediately after APVTS initialization.

3. Realtime Audio Thread Access: Load these parameters using relaxed memory ordering (`std::memory_order_relaxed`) at the very beginning of `processBlock` so they are immediately available for downstream MIDI transformation math.

4. UI Control Binding: Implement a custom, lock-free `ButtonGroupAttachment` helper class at the top of `ChordSettingsPanel.h` that implements `juce::AudioProcessorValueTreeState::Listener`. Use it to safely connect my `juce::OwnedArray<juce::TextButton>` button groups to their respective choice parameters on the message thread without stalling the audio loop.

Please provide the updated versions of these files, clearly highlighting the specific lines or blocks that were added or modified.

## Key Features

- Universal Compatibility: Effortlessly connect with your DAW and MIDI controller for a smooth and hassle-free setup.
- Chord Generation: From simple triads to complex jazz chords, create stunning chord progressions with ease.
- Smart Voicing: Intelligently selects the most pleasing chord voicings, ensuring your music always sounds professional.
- Spread, Humanize and Strum: Add depth and realism to your chords with adjustable spread, humanization and strumming effects.
- Strum Patterns: Choose from a variety of strum patterns to suit any genre or style, and bring your compositions to life.
- Key Signature: Easily play in any key signature, the perfect companion for songwriters and composers.

### MIDI Settings

Click the gear icon to enable/disable different MIDI inputs. By default all MIDI inputs are active. If you have multiple controllers and only want some to run through Chordable, you can deactivate them here by un-checking the Input box. Select "Input" if you want the controller to feed into Chordable and go to your DAW. Select "Output" if you want to send output from Chordable out to your MIDI controller. You can specify the MIDI Output Channel here as well, although typically you won't need to change this. If you do encounter hanging notes, you can use the "Send Note Offs" button to mute all notes.

### Preset Selection

Select a preset by clicking on this bar. Alternately, you can use the arrow buttons to navigate through the list of presets.

### Preset Menu

Clicking on the preset bar will open up this menu to select a preset. If you have Chordable Pro, you'll see your custom user presets on the right hand side.

Some presets may appear locked if you are using the free version of Chordable. If you have Chordable Pro, all presets will be available and you will have the ability to save your own custom presets.

### Save Preset

If you have Chordable Pro, you'll be able to save your own custom presets. This icon will become active whenever you change a default value from a preset. A dialogue will open up for you to specify the name of your saved preset.

### Scale Settings

These dropdowns set the root and mode of the current preset (i.e. the scale). If you change these values, it will transpose your current preset into the selected scale. When you change these values, you will see the gray keys in the first octave located in the bottom left-hand corner update to reflect the notes that are contained in the new scale, to serve as a guide so that you know which keys are in the scale.

These values are NOT part of the preset. So, if you have these set to D minor, for instance, loading a preset will not change those values; you'll stay in the D minor. This enables you to experiment with lots of presets while staying in your desired key.

**Note:** if you have a preset that uses the "pitch offset" feature, the "Root" and "Mode" parameters may be less meaningful, but can still be used to transpose your preset around the keyboard.

### Effect Bypass

If you turn this off, you will just hear the input notes as you play them. This can be useful if you are wanting to switch between recording melodies and chords.

### Chord-specific Controls

Changing these values only affects the chord associated with the current note that you are playing.

### Chord Type

This sets the type of chord associated with the currently selected note. Changing this selection will update the "Complexity" section with the different types of chords within the selected type of chord. Major and Minor chords are most commonly used in pop music. Dominant chords are more common in Jazz, and more exotic chords that are not part of any of the above are found in the "Other" category.

### Chord Complexity

For each chord type, there is a list of different complexities. Triads are the most basic chords, and the chords become more complex as more notes are added. You don't need to worry about the names if they are unfamiliar to you, they are listed generally in order of increasing complexity. 13th chords, for instance, are most commonly used in Jazz music. 7th chords, on the other hand, are now commonly found in pop music, lofi, hip hop, etc.

### Voicing

Select whether to use an "inversion" voicing. Inversions are basically when the notes in a chord are re-ordered so that the lowest note is not the root note. Skilled piano players often use inversions and can do so on the fly.

If you have Chordable Pro, you have access to the "Random" (?) and "Smart" (💡) controls, which can replicate how experienced piano players play. Random selects an inversion at random, Smart selects an inversion based on the previous chord played. Selecting Random or Smart will override the individual voicing settings for all chords.

### Pitch Offset

Use this control to shift the currently selected chord up or down by half-steps (only applies to current chord). This enables you to map multiple G chords within a preset, for instance.

### Global Controls

Changing these values will affect all chords.

### Spread

Sets how far the chord should span across the keyboard. A low value here will result in the minimum number of notes needed for the chord, within a ~1 octave span. Increasing this value will spread the notes over a larger span, based on the spread mode selected.

### Spread Mode

The left option (Dense) will result in dense chord voicings with many notes. The right option (Sparse) will result in sparser voicings with more space between the bass note and the rest of the chord. At Spread = 0, both modes are the same.

### Humanize

This controls randomizes the velocity and timing of the notes, making it sound more realistic. At Humanize = 0, the velocity of the chord notes will be equal to the input note, and will be evenly spaced in timing.

### Note Off Mode

With Simultaneous Release (left option), all chord notes will mute when you release the note. With Staggered Release (right option), the chord notes will mute one at a time. This option is most useful for quick arpeggios where you tap the note briefly. With Simultaneous Release, if you tap the note, the subsequent notes won't be triggered.

### Strum

Adjusts the timing in between chord notes. Tempo sync coming soon!

### Strum Mode

Chordable Pro feature — sets the order of notes that are played in the chord. The ? will select a Strum Mode at random. The 💡 will enable Smart Strum mode, which alternates between Strum Up and Strum Down based on the previous chord.

### Octave Map

The first octave in the bottom left corner shows you which note you are currently mapping. In the example here, you are currently mapping ALL C notes on the keyboard. The gray notes show you which notes are in the scale as defined in the upper right corner by the Root and Mode. This serves as a guide to help you know which notes are in the scale. If your preset is using the "Pitch Offset" feature, this guide may be less applicable.

### Keyboard

You can click on keys here to trigger chords, if you don't have a MIDI controller.

---

## UI Theme

**_Miami Nights (Radical Neon):_**
Inspired by the neon-drenched streets of 1980s pop culture and arcade floors, this palette swaps the dark violet for a punchy neon blue vibe paired with highlighter yellow.

- Midnight Arcade: #121214 (Chassis Background)
- Radical Blue: #0055FF (Modifier Keys / System Elements)
- Fluorescent Pink: #FF1493 (Alpha Keys / Main Text)
- Highlighter Yellow: #EEEE00 (Important UI Toggles / Secondary Legends)
- Bright Mint: #39FF14 (Success Indicators / Functional Highlights)

### Potential Layout

**_Matrix Layout Blueprint:_**

The Left Panel (ChordSettingsPanel) is split into a horizontal layout containing three logical child column groups:

1. Type Column: Vertical button stack (Major, Minor, Dominant, Other).
2. Complexity Grid: A strict 2x6 cell grid layout mapping out chord extensions (Triad, 7th, 9th, 11th, etc.).
3. Voicing & Pitch Stack: Vertical arrangement containing the Voicing stack (High, Root, Low) and the +/- Pitch Offset selector.

UI Implementation Code (ChordSettingsPanel)

This custom component manages its sub-layouts using juce::Grid for the item matrix and juce::FlexBox for the columns.

```c++
// ChordSettingsPanel.h
#pragma once
#include <JuceHeader.h>

class ChordSettingsPanel : public juce::Component
{
public:
    ChordSettingsPanel()
    {
        // 1. Type Column Buttons
        const juce::StringArray types { "Major", "Minor", "Dominant", "Other" };
        for (auto& type : types)
        {
            auto* b = typeButtons.add (std::make_unique<juce::TextButton> (type));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (1001); // Keeps buttons mutually exclusive
            addAndMakeVisible (b);
        }
        typeButtons[0]->setToggleState (true, juce::dontSendNotification); // Default to Major

        // 2. Complexity Matrix Grid Buttons
        const juce::StringArray complex { "Triad", "6th", "7th", "add9", "9th", "6over9",
                                          "11th", "13th", "7 b 5", "9 # 11", "13 # 11" };
        for (auto& comp : complex)
        {
            auto* b = complexityButtons.add (std::make_unique<juce::TextButton> (comp));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (1002);
            addAndMakeVisible (b);
        }

        // 3. Voicing Stack Buttons
        const juce::StringArray voices { "High", "Root", "Low" };
        for (auto& voice : voices)
        {
            auto* b = voicingButtons.add (std::make_unique<juce::TextButton> (voice));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (1003);
            addAndMakeVisible (b);
        }
        voicingButtons[1]->setToggleState (true, juce::dontSendNotification); // Default to Root

        // 4. Pitch Offset Buttons
        addAndMakeVisible (pitchDecButton);
        addAndMakeVisible (pitchIncButton);
        pitchDecButton.setButtonText ("-");
        pitchIncButton.setButtonText ("+");
    }

    ~ChordSettingsPanel() override = default;

    void paint (juce::Graphics& g) override
    {
        // Draw the visual border grouping outline matching the reference style
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xFF3B82F6)); // Vibrant Chordable Blue
        g.drawRoundedRectangle (bounds, 6.0f, 2.0f);

        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (14.0f);
        g.drawText ("Chord Settings", bounds.removeFromTop (25), juce::Justification::centred);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        bounds.reduce (10, 10);
        bounds.removeFromTop (20); // Make space for panel header text

        // Divide the panel horizontally into 3 primary section bounds
        auto columnWidth = bounds.getWidth() / 3;
        auto typeArea = bounds.removeFromLeft (columnWidth).reduced (5, 0);
        auto complexityArea = bounds.removeFromLeft (columnWidth).reduced (5, 0);
        auto voicingArea = bounds.reduced (5, 0);

        // ----------------------------------------------------
        // Layout 1: Type Stack (Vertical FlexBox)
        // ----------------------------------------------------
        juce::FlexBox typeFlex;
        typeFlex.flexDirection = juce::FlexBox::Direction::column;
        typeFlex.justifyContent = juce::FlexBox::JustifyContent::spaceAround;

        for (auto* btn : typeButtons)
            typeFlex.items.add (juce::FlexItem (*btn).withFlex (1.0f).withMargin (juce::FlexItem::Margin (4, 0, 4, 0)));

        typeFlex.performLayout (typeArea);

        // ----------------------------------------------------
        // Layout 2: Complexity Matrix (Two-Column Strict Grid)
        // ----------------------------------------------------
        juce::Grid compGrid;
        compGrid.flexDirection = juce::Grid::FlexDirection::row;
        compGrid.justifyContent = juce::Grid::JustifyContent::stretch;
        compGrid.alignItems = juce::Grid::AlignItems::stretch;

        // Define 2 equal columns and 6 equal rows
        using Track = juce::Grid::TrackInfo;
        compGrid.templateColumns = { Track (juce::Grid::Fr (1)), Track (juce::Grid::Fr (1)) };
        compGrid.templateRows = { Track (juce::Grid::Fr (1)), Track (juce::Grid::Fr (1)),
                                  Track (juce::Grid::Fr (1)), Track (juce::Grid::Fr (1)),
                                  Track (juce::Grid::Fr (1)), Track (juce::Grid::Fr (1)) };

        compGrid.setGap (juce::Grid::Px (6));

        for (auto* btn : complexityButtons)
            compGrid.items.add (juce::GridItem (*btn));

        compGrid.performLayout (complexityArea);

        // ----------------------------------------------------
        // Layout 3: Voicing + Pitch Offset (Split Column)
        // ----------------------------------------------------
        auto voicingStackArea = voicingArea.removeFromTop (voicingArea.getHeight() * 0.7);
        auto pitchArea = voicingArea.reduced (0, 5);

        // Voicing Vertical Stack
        juce::FlexBox voiceFlex;
        voiceFlex.flexDirection = juce::FlexBox::Direction::column;
        voiceFlex.justifyContent = juce::FlexBox::JustifyContent::spaceAround;

        for (auto* btn : voicingButtons)
            voiceFlex.items.add (juce::FlexItem (*btn).withFlex (1.0f).withMargin (juce::FlexItem::Margin (2)));

        voiceFlex.performLayout (voicingStackArea);

        // Pitch Offset Horizontal Controls row
        juce::FlexBox pitchFlex;
        pitchFlex.flexDirection = juce::FlexBox::Direction::row;
        pitchFlex.justifyContent = juce::FlexBox::JustifyContent::center;

        pitchFlex.items.add (juce::FlexItem (pitchDecButton).withFlex (1.0f).withMargin (2));
        pitchFlex.items.add (juce::FlexItem (pitchIncButton).withFlex (1.0f).withMargin (2));

        pitchFlex.performLayout (pitchArea);
    }

private:
    juce::OwnedArray<juce::TextButton> typeButtons;
    juce::OwnedArray<juce::TextButton> complexityButtons;
    juce::OwnedArray<juce::TextButton> voicingButtons;

    juce::TextButton pitchDecButton;
    juce::TextButton pitchIncButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordSettingsPanel)
};
```

---

## Phase X: Target Deployments & MIDI Engine Architecture

### 1. Build Target Properties & DAW Hook Optimization

- Configure CMake using `juce_add_plugin` targeting VST3, AU, and Standalone formats simultaneously.
- Enforce compilation constraints for pure MIDI manipulation:
  - Set `IS_SYNTH FALSE` to decouple the layout from audio output buses.
  - Set `IS_MIDI_EFFECT TRUE` to guarantee Apple Logic Pro instantiates the module inside the dedicated "MIDI FX" channel slot.
  - Set `NEEDS_MIDI_INPUT TRUE` and `NEEDS_MIDI_OUTPUT TRUE` to unlock raw MIDI buffer manipulation.

### 2. Standalone Application Integration & System Inter-op

- Leverage JUCE's native `juce::StandaloneFilterWindow` class mappings to wrap the UI layout.
- Incorporate a `juce::AudioDeviceSelectorComponent` inside a settings modal or layout drawer so users can bind physical MIDI controller inputs.
- On macOS platforms, implement an explicit call to `juce::MidiOutput::createNewDevice("Wicky Chords Virtual Out")` inside the standalone setup lifecycle. This creates a virtual system-wide MIDI port, allowing the standalone app to pipe processed chords directly into external third-party standalone synths or background standalone engines.

### 3. Real-Time Thread Safety & Buffer Mutation Invalidation

- Enforce absolute zero-allocation constraints inside the `processBlock` loop to ensure safe audio-thread performance.
- Implement a "Copy-then-Swap" processing loop using a preallocated, bounded member buffer (`juce::MidiBuffer`) to manipulate MIDI data. This ensures the engine never modifies a MIDI buffer mid-iteration, preventing iterator invalidation crashes.

---

### Resources

1. [Documentation and Reference Images for Functionality](https://mattpfeifferapps.com/chordable/docs/)
2. [UI Layout Image](https://mattpfeifferapps.com/images/chordable/chordable_main_image.png)
3. **_Skills_** are located in `./skills/`.
4. Source code is located at `./Source/`.

#### Notes

- Our plugin is named "Wicky Chordinator".
- Our companies name is "Wickman Wonderworks".
- When development is complete you should create a user guide `USER_GUIDE.md` that explains how to use the tool. The instructions should be easy to understand, the easier it is to use our software the more they will use it.

Ask me if you have any questions or need further guidance.
