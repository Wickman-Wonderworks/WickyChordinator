#include "PluginProcessor.h"
#include "ChordSettingsPanel.h"
#include "DSP/GainProcessor.h"
#include "State/ParameterIDs.h"
#include "UI/OxaniumFonts.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

class PluginProcessorTestAccess
{
public:
    static const void* outputStorage (PluginProcessor& processor)
    {
        return processor.midiOutputBuffer.data.begin();
    }
};

namespace
{
    void require (bool condition, const char* message)
    {
        if (! condition)
            throw std::runtime_error (message);
    }

    void expectSamples (const juce::AudioBuffer<float>& buffer, float expected)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                require (std::isfinite (buffer.getSample (channel, sample))
                         && std::abs (buffer.getSample (channel, sample) - expected) < 1.0e-5f,
                         "Unexpected audio sample");
    }

    void fill (juce::AudioBuffer<float>& buffer, float value)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample (channel, sample, value);
    }

    void setParameter (PluginProcessor& processor, const char* id, float value)
    {
        auto* parameter = processor.getValueTreeState().getParameter (id);
        require (parameter != nullptr, "Missing parameter");
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    void testGain()
    {
        boilerplate::dsp::GainProcessor gain;
        juce::AudioBuffer<float> buffer (2, 64);

        // A saved bypass must pass audio unchanged from the very first sample.
        gain.prepare (48000.0, -60.0f, true);
        fill (buffer, 0.25f);
        gain.process (buffer, -60.0f, true);
        expectSamples (buffer, 0.25f);

        // Re-preparation must discard both the ramp and cached parameter state.
        gain.prepare (48000.0, -6.0f, false);
        fill (buffer, 1.0f);
        gain.process (buffer, -6.0f, false);
        expectSamples (buffer, juce::Decibels::decibelsToGain (-6.0f));

        // At 1 kHz the 20 ms ramp must reach its endpoint on sample 20.
        gain.prepare (1000.0, 0.0f, false);
        buffer.setSize (2, 20);
        fill (buffer, 1.0f);
        gain.process (buffer, -60.0f, false);
        float previous = 1.0f;
        for (int sample = 0; sample < 20; ++sample)
        {
            const auto value = buffer.getSample (0, sample);
            require (value <= previous && value >= 0.0f, "Gain ramp is not monotonic");
            require (juce::exactlyEqual (value, buffer.getSample (1, sample)),
                     "Smoothing differs between channels");
            previous = value;
        }
        require (std::abs (previous - 0.001f) < 1.0e-6f, "Incorrect ramp endpoint");

        // Changing gain while bypassed must be reflected after bypass is removed.
        fill (buffer, 1.0f);
        gain.process (buffer, 12.0f, true);
        require (std::abs (buffer.getSample (0, 19) - 1.0f) < 1.0e-6f, "Bypass ramp failed");
        fill (buffer, 1.0f);
        gain.process (buffer, 12.0f, false);
        const auto boosted = juce::Decibels::decibelsToGain (12.0f);
        require (std::abs (buffer.getSample (0, 19) - boosted) < 1.0e-5f, "Cached gain is stale");
        fill (buffer, 1.0f);
        gain.process (buffer, 12.0f, false);
        expectSamples (buffer, boosted);

        for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
            for (const auto decibels : { -60.0f, 0.0f, 12.0f })
                for (const auto channels : { 1, 2 })
                {
                    gain.prepare (sampleRate, decibels, false);
                    for (const auto samples : { 0, 1, 17, 512, 2048 })
                    {
                        buffer.setSize (channels, samples);
                        fill (buffer, 0.5f);
                        gain.process (buffer, decibels, false);
                        expectSamples (buffer, 0.5f * juce::Decibels::decibelsToGain (decibels));
                        buffer.clear();
                        gain.process (buffer, decibels, false);
                        expectSamples (buffer, 0.0f);
                    }
                }
    }

    struct CapturedEvent
    {
        int position = 0;
        int status = 0;
        int channel = 0;
        int data1 = 0;
        int data2 = 0;
    };

    std::vector<CapturedEvent> captureMidi (const juce::MidiBuffer& midi)
    {
        std::vector<CapturedEvent> events;

        for (const auto metadata : midi)
        {
            if (metadata.numBytes < 2)
                continue;

            CapturedEvent event;
            event.position = metadata.samplePosition;
            event.status = metadata.data[0] & 0xf0;
            event.channel = metadata.data[0] & 0x0f;
            event.data1 = metadata.data[1];
            event.data2 = metadata.numBytes > 2 ? metadata.data[2] : 0;
            events.push_back (event);
        }

        return events;
    }

    bool hasEvent (const std::vector<CapturedEvent>& events, int status, int channel,
                   int data1, int data2 = -1)
    {
        for (const auto& event : events)
            if (event.status == status && event.channel == channel
                && event.data1 == data1 && (data2 < 0 || event.data2 == data2))
                return true;

        return false;
    }

    bool hasNoteOn (const std::vector<CapturedEvent>& events, int channel, int note)
    {
        for (const auto& event : events)
            if (event.status == 0x90 && event.channel == channel
                && event.data1 == note && event.data2 > 0)
                return true;

        return false;
    }

    bool hasNoteOff (const std::vector<CapturedEvent>& events, int channel, int note)
    {
        for (const auto& event : events)
            if ((event.status == 0x80 || (event.status == 0x90 && event.data2 == 0))
                && event.channel == channel && event.data1 == note)
                return true;

        return false;
    }

    int countNoteOns (const std::vector<CapturedEvent>& events)
    {
        int count = 0;

        for (const auto& event : events)
            if (event.status == 0x90 && event.data2 > 0)
                ++count;

        return count;
    }

    std::vector<CapturedEvent> processMessages (PluginProcessor& processor,
                                                std::initializer_list<juce::MidiMessage> messages)
    {
        juce::MidiBuffer midi;
        int position = 0;

        for (const auto& message : messages)
            midi.addEvent (message, position++);

        juce::AudioBuffer<float> buffer (2, 64);
        processor.processBlock (buffer, midi);
        return captureMidi (midi);
    }

    void setChordMatrix (PluginProcessor& processor, int type, int complexity,
                         int voicing, int pitchOffset)
    {
        // Existing sound-contract tests deliberately exercise legacy register voicing.
        setParameter (processor, boilerplate::parameters::voicingStyle, 0.0f);
        setParameter (processor, boilerplate::parameters::humanizeTiming, 0.0f);
        setParameter (processor, boilerplate::parameters::chordType, (float) type);
        setParameter (processor, boilerplate::parameters::complexity, (float) complexity);
        setParameter (processor, boilerplate::parameters::voicing, (float) voicing);
        setParameter (processor, boilerplate::parameters::pitchOffset, (float) pitchOffset);
        setParameter (processor, boilerplate::parameters::gain, 0.0f);
        setParameter (processor, boilerplate::parameters::bypass, 0.0f);
    }

    void testMidiVelocityGain()
    {
        PluginProcessor processor;
        processor.prepareToPlay (48000.0, 64);
        setChordMatrix (processor, 0, 0, 1, 0);

        auto scaledVelocityFor = [&] (float gainDb, int velocity)
        {
            setParameter (processor, boilerplate::parameters::gain, gainDb);
            setParameter (processor, boilerplate::parameters::bypass, 0.0f);

            const auto events = processMessages (processor, {
                juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (velocity)) });

            int expected = -1;
            int noteOnCount = 0;

            for (const auto& event : events)
            {
                if (event.status == 0x90 && event.data2 > 0)
                {
                    if (expected < 0)
                        expected = event.data2;

                    require (event.data2 == expected, "Chord notes have different velocities");
                    ++noteOnCount;
                }
            }

            require (noteOnCount == 3, "Chord did not produce three note-ons");
            processMessages (processor, { juce::MidiMessage::noteOff (1, 60) });
            return expected;
        };

        require (scaledVelocityFor (0.0f, 100) == 100, "0 dB changed velocity");
        require (scaledVelocityFor (-6.0f, 100) == 50, "Velocity attenuation failed");
        require (scaledVelocityFor (12.0f, 100) == 127, "Velocity boost did not clamp");
        require (scaledVelocityFor (-60.0f, 100) == 1, "Full attenuation lost the note-on");

        // Bypass is a transparent pass-through: one note-on, one CC, one note-off.
        setParameter (processor, boilerplate::parameters::gain, -24.0f);
        setParameter (processor, boilerplate::parameters::bypass, 1.0f);

        juce::AudioBuffer<float> buffer (2, 64);
        fill (buffer, 0.25f);

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 64), 1);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 2);
        processor.processBlock (buffer, midi);

        const auto bypassed = captureMidi (midi);
        require (countNoteOns (bypassed) == 1, "Bypass generated extra notes");
        require (hasEvent (bypassed, 0x90, 0, 60, 100), "Bypass changed the note-on");
        require (hasEvent (bypassed, 0xB0, 0, 1, 64), "Bypass dropped a controller");
        require (hasNoteOff (bypassed, 0, 60), "Bypass dropped the note-off");
        expectSamples (buffer, JucePlugin_IsSynth ? 0.0f : 0.25f);

        setParameter (processor, boilerplate::parameters::bypass, 0.0f);
    }

    void testChordEngine()
    {
        PluginProcessor processor;
        processor.prepareToPlay (48000.0, 64);

        const auto noteOn = [] (int channel, int note, int velocity = 100)
        {
            return juce::MidiMessage::noteOn (channel, note,
                                              static_cast<juce::uint8> (velocity));
        };

        const auto noteOff = [] (int channel, int note)
        {
            return juce::MidiMessage::noteOff (channel, note);
        };

        // Root position, major triad. The input note is replaced by the chord.
        setChordMatrix (processor, 0, 0, 1, 0);
        auto events = processMessages (processor, { noteOn (1, 60) });
        require (events.size() == 3, "Input note was not replaced by a triad");
        require (hasNoteOn (events, 0, 60) && hasNoteOn (events, 0, 64)
                 && hasNoteOn (events, 0, 67), "Incorrect C major triad");
        require (processor.getLastTriggeredChordNote() == 60,
                 "Last chord note was not recorded");
        require (processor.getNumSoundingChords() > 0,
                 "Sounding chord count was not recorded");

        const auto lastRecipe = processor.getLastTriggeredChordRecipe();
        require (lastRecipe.chordType == 0 && lastRecipe.complexity == 0
                 && lastRecipe.voicing == 1 && lastRecipe.pitchOffset == 0,
                 "Last chord recipe was not recorded");

        // Per-key assignment: D4 gets its own root without changing the matrix.
        auto dEvents = processMessages (processor, { noteOn (1, 62) });
        require (hasNoteOn (dEvents, 0, 62) && hasNoteOn (dEvents, 0, 66)
                 && hasNoteOn (dEvents, 0, 69), "Per-key root mapping failed");

        auto releaseC = processMessages (processor, { noteOff (1, 60) });
        require (hasNoteOff (releaseC, 0, 60) && hasNoteOff (releaseC, 0, 64)
                 && hasNoteOff (releaseC, 0, 67), "C chord did not release");

        auto releaseD = processMessages (processor, { noteOff (1, 62) });
        require (hasNoteOff (releaseD, 0, 62) && hasNoteOff (releaseD, 0, 66)
                 && hasNoteOff (releaseD, 0, 69), "D chord did not release");

        // Complexity and type tables.
        setChordMatrix (processor, 0, 2, 1, 0); // Major 7th
        events = processMessages (processor, { noteOn (1, 60) });
        require (hasNoteOn (events, 0, 60) && hasNoteOn (events, 0, 64)
                 && hasNoteOn (events, 0, 67) && hasNoteOn (events, 0, 71),
                 "Major 7th table is wrong");
        processMessages (processor, { noteOff (1, 60) });

        setChordMatrix (processor, 2, 2, 1, 0); // Dominant 7th
        events = processMessages (processor, { noteOn (1, 60) });
        require (hasNoteOn (events, 0, 60) && hasNoteOn (events, 0, 64)
                 && hasNoteOn (events, 0, 67) && hasNoteOn (events, 0, 70),
                 "Dominant 7th table is wrong");
        processMessages (processor, { noteOff (1, 60) });

        // Voicing register and pitch offset.
        setChordMatrix (processor, 0, 0, 0, 0); // High
        events = processMessages (processor, { noteOn (1, 60) });
        require (hasNoteOn (events, 0, 72) && hasNoteOn (events, 0, 76)
                 && hasNoteOn (events, 0, 79), "High voicing is wrong");
        processMessages (processor, { noteOff (1, 60) });

        setChordMatrix (processor, 0, 0, 2, 0); // Low
        events = processMessages (processor, { noteOn (1, 60) });
        require (hasNoteOn (events, 0, 48) && hasNoteOn (events, 0, 52)
                 && hasNoteOn (events, 0, 55), "Low voicing is wrong");
        processMessages (processor, { noteOff (1, 60) });

        setChordMatrix (processor, 0, 0, 1, 2); // +2 semitones
        events = processMessages (processor, { noteOn (1, 60) });
        require (hasNoteOn (events, 0, 62) && hasNoteOn (events, 0, 66)
                 && hasNoteOn (events, 0, 69), "Pitch offset is wrong");
        processMessages (processor, { noteOff (1, 60) });

        // C major (C E G) and E major (E G# B) share E: refcount keeps it on.
        setChordMatrix (processor, 0, 0, 1, 0);
        processMessages (processor, { noteOn (1, 60) });

        auto eEvents = processMessages (processor, { noteOn (1, 64) });
        require (hasNoteOn (eEvents, 0, 68) && hasNoteOn (eEvents, 0, 71),
                 "E major did not join the shared note");
        require (! hasNoteOn (eEvents, 0, 64), "Shared note was retriggered");

        releaseC = processMessages (processor, { noteOff (1, 60) });
        require (hasNoteOff (releaseC, 0, 60) && hasNoteOff (releaseC, 0, 67),
                 "C release dropped the wrong notes");
        require (! hasNoteOff (releaseC, 0, 64), "Shared note was cut early");

        auto releaseE = processMessages (processor, { noteOff (1, 64) });
        require (hasNoteOff (releaseE, 0, 64) && hasNoteOff (releaseE, 0, 68)
                 && hasNoteOff (releaseE, 0, 71), "E release failed");

        // Retriggering a held key replaces its chord without orphaning notes.
        processMessages (processor, { noteOn (1, 60) });
        auto retrigger = processMessages (processor, { noteOn (1, 60) });
        require (hasNoteOff (retrigger, 0, 60) && hasNoteOff (retrigger, 0, 64)
                 && hasNoteOff (retrigger, 0, 67),
                 "Retrigger did not release the old chord");
        require (hasNoteOn (retrigger, 0, 60) && hasNoteOn (retrigger, 0, 64)
                 && hasNoteOn (retrigger, 0, 67),
                 "Retrigger did not start the new chord");
        processMessages (processor, { noteOff (1, 60) });

        // A note-off for a generated, non-trigger note must not cut that note.
        processMessages (processor, { noteOn (1, 60) });
        auto strayOff = processMessages (processor, { noteOff (1, 64) });
        require (strayOff.empty(), "Stray note-off cut a generated note");

        auto finalRelease = processMessages (processor, { noteOff (1, 60) });
        require (hasNoteOff (finalRelease, 0, 60) && hasNoteOff (finalRelease, 0, 64)
                 && hasNoteOff (finalRelease, 0, 67), "Final release failed");

        // CC123 releases every chord on its channel and is forwarded.
        processMessages (processor, { noteOn (1, 60) });
        auto panicEvents = processMessages (processor, { juce::MidiMessage::allNotesOff (1) });
        require (hasNoteOff (panicEvents, 0, 60) && hasNoteOff (panicEvents, 0, 64)
                 && hasNoteOff (panicEvents, 0, 67),
                 "All-notes-off did not release the chord");
        require (hasEvent (panicEvents, 0xB0, 0, 123, 0), "All-notes-off was not forwarded");

        // Bypass releases chord notes and passes the input through untouched.
        processMessages (processor, { noteOn (1, 60) });
        setParameter (processor, boilerplate::parameters::bypass, 1.0f);

        auto bypassEvents = processMessages (processor, { noteOn (1, 62) });
        require (hasNoteOff (bypassEvents, 0, 60) && hasNoteOff (bypassEvents, 0, 64)
                 && hasNoteOff (bypassEvents, 0, 67), "Bypass orphaned the chord");
        require (hasNoteOn (bypassEvents, 0, 62), "Bypass did not pass the input");
        require (! hasNoteOn (bypassEvents, 0, 66), "Bypass still generated a chord");

        setParameter (processor, boilerplate::parameters::bypass, 0.0f);
        auto passThroughOff = processMessages (processor, { noteOff (1, 62) });
        require (hasNoteOff (passThroughOff, 0, 62), "Bypass note-off was lost");

        // Channels are independent.
        setChordMatrix (processor, 0, 0, 1, 0);
        processMessages (processor, { noteOn (1, 60) });

        auto channelTwoOn = processMessages (processor, { noteOn (2, 60) });
        require (hasNoteOn (channelTwoOn, 1, 60) && hasNoteOn (channelTwoOn, 1, 64)
                 && hasNoteOn (channelTwoOn, 1, 67), "Channel 2 chord missing");

        auto channelOneOff = processMessages (processor, { noteOff (1, 60) });
        require (hasNoteOff (channelOneOff, 0, 60) && hasNoteOff (channelOneOff, 0, 64)
                 && hasNoteOff (channelOneOff, 0, 67), "Channel 1 release failed");
        require (! hasNoteOff (channelOneOff, 1, 60), "Channel 1 release touched channel 2");

        auto channelTwoOff = processMessages (processor, { noteOff (2, 60) });
        require (hasNoteOff (channelTwoOff, 1, 60) && hasNoteOff (channelTwoOff, 1, 64)
                 && hasNoteOff (channelTwoOff, 1, 67), "Channel 2 release failed");

        // High keys fold by octaves instead of producing out-of-range notes.
        setChordMatrix (processor, 0, 0, 1, 0);
        events = processMessages (processor, { noteOn (1, 125) });
        require (hasNoteOn (events, 0, 113) && hasNoteOn (events, 0, 117)
                 && hasNoteOn (events, 0, 120), "High-key octave folding is wrong");

        for (const auto& event : events)
            require (event.data1 >= 0 && event.data1 <= 127, "Out-of-range MIDI note");

        processMessages (processor, { noteOff (1, 125) });

        // Smoke test every type/complexity for note balance.
        for (int type = 0; type < 4; ++type)
        {
            for (int complexity = 0; complexity < 11; ++complexity)
            {
                setChordMatrix (processor, type, complexity, 1, 0);

                auto on = processMessages (processor, { noteOn (1, 60) });
                auto off = processMessages (processor, { noteOff (1, 60) });

                require (! on.empty(), "Chord shape produced no notes");
                require (on.size() == off.size(), "Chord note release count mismatch");
            }
        }
    }

    void testPerKeyRecipes()
    {
        PluginProcessor processor;
        processor.prepareToPlay (48000.0, 64);
        setChordMatrix (processor, 0, 0, 1, 0); // global: Major triad, Root

        // Override C4 only: Minor 7th, High voicing, +2 semitones.
        processor.setKeyRecipe (60, { 1, 2, 0, 2 });
        require (processor.hasKeyRecipe (60), "Override was not stored");
        require (! processor.hasKeyRecipe (62), "Unexpected override on D4");

        const auto recipe = processor.getKeyRecipe (60);
        require (recipe.chordType == 1 && recipe.complexity == 2
                 && recipe.voicing == 0 && recipe.pitchOffset == 2,
                 "Override readback is wrong");

        // C4 uses the override: 60 + 2 + 12 = 74, minor 7th = 74/77/81/84.
        auto cEvents = processMessages (processor, {
            juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)) });
        require (hasNoteOn (cEvents, 0, 74) && hasNoteOn (cEvents, 0, 77)
                 && hasNoteOn (cEvents, 0, 81) && hasNoteOn (cEvents, 0, 84),
                 "Per-key recipe was not applied");
        processMessages (processor, { juce::MidiMessage::noteOff (1, 60) });

        // D4 must still use the global matrix.
        auto dEvents = processMessages (processor, {
            juce::MidiMessage::noteOn (1, 62, static_cast<juce::uint8> (100)) });
        require (hasNoteOn (dEvents, 0, 62) && hasNoteOn (dEvents, 0, 66)
                 && hasNoteOn (dEvents, 0, 69), "Global fallback failed");
        processMessages (processor, { juce::MidiMessage::noteOff (1, 62) });

        // C5 is a separate slot and must not inherit C4's override.
        auto c5Events = processMessages (processor, {
            juce::MidiMessage::noteOn (1, 72, static_cast<juce::uint8> (100)) });
        require (hasNoteOn (c5Events, 0, 72) && hasNoteOn (c5Events, 0, 76)
                 && hasNoteOn (c5Events, 0, 79), "C5 incorrectly used C4's override");
        processMessages (processor, { juce::MidiMessage::noteOff (1, 72) });

        // Full state round-trip through the ValueTree.
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        require (state.getSize() > 0, "Empty state");

        PluginProcessor restored;
        restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        restored.prepareToPlay (48000.0, 64);

        require (restored.hasKeyRecipe (60), "Recipe was not restored");
        require (! restored.hasKeyRecipe (62), "Restore created a stray recipe");

        const auto restoredRecipe = restored.getKeyRecipe (60);
        require (restoredRecipe.chordType == 1 && restoredRecipe.complexity == 2
                 && restoredRecipe.voicing == 0 && restoredRecipe.pitchOffset == 2,
                 "Restored recipe values are wrong");

        auto restoredEvents = processMessages (restored, {
            juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)) });
        require (hasNoteOn (restoredEvents, 0, 74) && hasNoteOn (restoredEvents, 0, 77)
                 && hasNoteOn (restoredEvents, 0, 81) && hasNoteOn (restoredEvents, 0, 84),
                 "Restored recipe was not applied");
        processMessages (restored, { juce::MidiMessage::noteOff (1, 60) });

        // An older state without a KeyRecipes child must clear stale overrides.
        auto legacyTree = juce::ValueTree::readFromData (state.getData(),
                                                         static_cast<size_t> (state.getSize()));
        const auto recipesChild = legacyTree.getChildWithName (juce::Identifier ("KeyRecipes"));
        require (recipesChild.isValid(), "Missing KeyRecipes child");
        legacyTree.removeChild (recipesChild, nullptr);

        juce::MemoryBlock legacyState;
        {
            juce::MemoryOutputStream stream (legacyState, false);
            legacyTree.writeToStream (stream);
        }

        PluginProcessor legacy;
        legacy.setKeyRecipe (60, { 2, 4, 2, -3 });
        require (legacy.hasKeyRecipe (60), "Legacy setup failed");
        legacy.setStateInformation (legacyState.getData(),
                                    static_cast<int> (legacyState.getSize()));
        require (! legacy.hasKeyRecipe (60), "Legacy state kept a stale override");

        // Clearing an override returns the key to the global matrix.
        processor.clearKeyRecipe (60);
        require (! processor.hasKeyRecipe (60), "Recipe was not cleared");

        auto clearedEvents = processMessages (processor, {
            juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)) });
        require (hasNoteOn (clearedEvents, 0, 60) && hasNoteOn (clearedEvents, 0, 64)
                 && hasNoteOn (clearedEvents, 0, 67), "Cleared key did not fall back");
        processMessages (processor, { juce::MidiMessage::noteOff (1, 60) });
    }

    void testOxaniumFont()
    {
        const auto regular = boilerplate::ui::fonts::oxaniumTypeface (400.0f);
        require (regular != nullptr, "Oxanium regular typeface failed to load");
        require (regular->getName().containsIgnoreCase ("Oxanium"),
                 "Embedded typeface is not Oxanium");

        const auto bold = boilerplate::ui::fonts::oxaniumTypeface (700.0f);
        require (bold != nullptr, "Oxanium bold typeface failed to load");
    }

    void testVirtualKeyboardBridge()
    {
        PluginProcessor processor;
        processor.prepareToPlay (48000.0, 64);
        setChordMatrix (processor, 0, 0, 1, 0); // C major triad, root position.

        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer midi;

        processor.queueKeyboardNote (true, 60, 0.8f);
        processor.processBlock (buffer, midi);

        auto events = captureMidi (midi);
        require (hasNoteOn (events, 0, 60) && hasNoteOn (events, 0, 64)
                 && hasNoteOn (events, 0, 67),
                 "Virtual keyboard note did not generate a chord");
        require ((processor.getActiveInputNotes (0) & (juce::uint64 (1) << 60)) != 0,
                 "Virtual keyboard note was not mirrored to the UI mask");
        require ((processor.getActiveOutputNotes (0) & (juce::uint64 (1) << 60)) != 0
                 && (processor.getActiveOutputNotes (1) & (juce::uint64 (1) << 0)) != 0  // 64
                 && (processor.getActiveOutputNotes (1) & (juce::uint64 (1) << 3)) != 0, // 67
                 "Processed chord was not mirrored to the keyboard display mask");

        processor.queueKeyboardNote (false, 60, 0.0f);
        midi.clear();
        processor.processBlock (buffer, midi);

        events = captureMidi (midi);
        require (hasNoteOff (events, 0, 60) && hasNoteOff (events, 0, 64)
                 && hasNoteOff (events, 0, 67),
                 "Virtual keyboard note-off did not release the chord");
        require ((processor.getActiveInputNotes (0) & (juce::uint64 (1) << 60)) == 0,
                 "Virtual keyboard note-off was not mirrored to the UI mask");
        require ((processor.getActiveOutputNotes (0) & (juce::uint64 (1) << 60)) == 0
                 && (processor.getActiveOutputNotes (1) & (juce::uint64 (1) << 0)) == 0  // 64
                 && (processor.getActiveOutputNotes (1) & (juce::uint64 (1) << 3)) == 0, // 67
                 "Released chord was not cleared from the keyboard display mask");
    }

    void testInteractionEngine()
    {
        PluginProcessor processor;
        processor.prepareToPlay (48000.0, 64);
        setChordMatrix (processor, 0, 0, 1, 0); // C major triad, root position.

        const auto noteOn = [] (int channel, int note, int velocity = 100)
        {
            return juce::MidiMessage::noteOn (channel, note,
                                              static_cast<juce::uint8> (velocity));
        };

        const auto noteOff = [] (int channel, int note)
        {
            return juce::MidiMessage::noteOff (channel, note);
        };

        // Spread = 100, Dense: the upper voices open by one and two octaves.
        setParameter (processor, boilerplate::parameters::spread, 100.0f);
        setParameter (processor, boilerplate::parameters::spreadMode, 0.0f);
        auto spreadEvents = processMessages (processor, { noteOn (1, 60) });
        require (hasNoteOn (spreadEvents, 0, 60)
                 && hasNoteOn (spreadEvents, 0, 76)
                 && hasNoteOn (spreadEvents, 0, 91),
                 "Dense spread voicing is wrong");
        processMessages (processor, { noteOff (1, 60) });
        setParameter (processor, boilerplate::parameters::spread, 0.0f);

        // Humanize must vary velocities inside a chord. Use a seven-note shape
        // so a single chord contains enough rolls of the PRNG to observe it.
        setChordMatrix (processor, 0, 10, 1, 0); // Major 13#11
        setParameter (processor, boilerplate::parameters::humanize, 100.0f);

        bool sawVelocityVariation = false;
        auto humanized = processMessages (processor, { noteOn (1, 60) });
        int firstVelocity = -1;

        for (const auto& event : humanized)
        {
            if (event.status != 0x90 || event.data2 <= 0)
                continue;

            if (firstVelocity < 0)
                firstVelocity = event.data2;
            else if (event.data2 != firstVelocity)
                sawVelocityVariation = true;
        }

        require (sawVelocityVariation, "Humanize did not vary chord velocities");
        processMessages (processor, { noteOff (1, 60) });
        setParameter (processor, boilerplate::parameters::humanize, 0.0f);

        // Strum Up: only the root is allowed through on the first block, and
        // the remaining tones arrive over subsequent blocks.
        setChordMatrix (processor, 0, 0, 1, 0);
        setParameter (processor, boilerplate::parameters::strum, 100.0f);
        setParameter (processor, boilerplate::parameters::strumMode, 0.0f);

        auto firstStrumBlock = processMessages (processor, { noteOn (1, 60) });
        require (countNoteOns (firstStrumBlock) == 1,
                 "Strum emitted more than one tone in the first block");
        require (hasNoteOn (firstStrumBlock, 0, 60),
                 "Strum Up did not start from the lowest chord tone");

        std::vector<CapturedEvent> strumEvents = firstStrumBlock;

        for (int block = 0; block < 120; ++block)
        {
            auto pending = processMessages (processor, {});
            strumEvents.insert (strumEvents.end(), pending.begin(), pending.end());
        }

        require (hasNoteOn (strumEvents, 0, 60)
                 && hasNoteOn (strumEvents, 0, 64)
                 && hasNoteOn (strumEvents, 0, 67),
                 "Strum Up did not deliver every chord tone");
        processMessages (processor, { noteOff (1, 60) });

        // Strum Down starts from the highest chord tone.
        setParameter (processor, boilerplate::parameters::strumMode, 1.0f);
        auto downStrumBlock = processMessages (processor, { noteOn (1, 60) });
        require (countNoteOns (downStrumBlock) == 1,
                 "Down strum emitted more than one tone in the first block");
        require (hasNoteOn (downStrumBlock, 0, 67),
                 "Strum Down did not start from the highest chord tone");

        for (int block = 0; block < 120; ++block)
            processMessages (processor, {});

        processMessages (processor, { noteOff (1, 60) });
        setParameter (processor, boilerplate::parameters::strum, 0.0f);
        setParameter (processor, boilerplate::parameters::strumMode, 0.0f);

        // Staggered release: the first note-off is immediate, the remaining
        // chord tones release over later blocks.
        setParameter (processor, boilerplate::parameters::noteOffMode, 1.0f);
        processMessages (processor, { noteOn (1, 60) });

        auto firstOffBlock = processMessages (processor, { noteOff (1, 60) });
        require (! firstOffBlock.empty(), "Staggered release emitted no note-off");
        require (hasNoteOff (firstOffBlock, 0, 60),
                 "Staggered release did not start at the lowest chord tone");

        std::vector<CapturedEvent> staggeredOffs = firstOffBlock;

        for (int block = 0; block < 120; ++block)
        {
            auto pending = processMessages (processor, {});
            staggeredOffs.insert (staggeredOffs.end(), pending.begin(), pending.end());
        }

        require (hasNoteOff (staggeredOffs, 0, 60)
                 && hasNoteOff (staggeredOffs, 0, 64)
                 && hasNoteOff (staggeredOffs, 0, 67),
                 "Staggered release did not deliver every note-off");

        setParameter (processor, boilerplate::parameters::noteOffMode, 0.0f);

        // The global power switch must behave like bypass: one pass-through
        // note instead of a generated chord.
        setParameter (processor, boilerplate::parameters::power, 0.0f);
        auto poweredOff = processMessages (processor, { noteOn (1, 60) });
        require (countNoteOns (poweredOff) == 1
                 && hasNoteOn (poweredOff, 0, 60)
                 && ! hasNoteOn (poweredOff, 0, 64),
                 "Power OFF did not pass the input through");
        processMessages (processor, { noteOff (1, 60) });
        setParameter (processor, boilerplate::parameters::power, 1.0f);
    }

    void testPresetManager()
    {
        PluginProcessor processor;
        processor.prepareToPlay (48000.0, 64);
        processor.setKeyRecipe (60, { 2, 4, 2, -3 });

        wicky::ui::PresetManager presets (processor.getValueTreeState());
        require (presets.getNumPresets() > 1, "Preset bank is empty");
        require (presets.getName (0) == "Init", "First preset is not Init");

        require (presets.loadPreset (1), "Could not load the second preset");
        require (presets.getCurrentIndex() == 1, "Preset index did not advance");

        auto& parameters = processor.getValueTreeState();
        require (std::abs (parameters.getRawParameterValue (
                               boilerplate::parameters::spread)->load() - 32.0f) < 0.01f,
                 "Preset did not apply Spread");
        require (std::abs (parameters.getRawParameterValue (
                               boilerplate::parameters::strum)->load() - 22.0f) < 0.01f,
                 "Preset did not apply Strum");

        // Loading a global preset must not erase per-key recipes.
        const auto recipe = processor.getKeyRecipe (60);
        require (recipe.chordType == 2 && recipe.complexity == 4
                 && recipe.voicing == 2 && recipe.pitchOffset == -3,
                 "Preset load destroyed a per-key recipe");

        require (presets.loadPrevious(), "Previous preset navigation failed");
        require (presets.getCurrentIndex() == 0, "Previous preset index is wrong");

        require (presets.loadNext(), "Next preset navigation failed");
        require (presets.getCurrentIndex() == 1, "Next preset index is wrong");

        // A preset manager created while the live state is modified must still
        // offer a deterministic Init preset based on parameter defaults.
        wicky::ui::PresetManager rebuiltPresets (processor.getValueTreeState());
        require (rebuiltPresets.loadPreset (0), "Could not load Init");
        require (std::abs (parameters.getRawParameterValue (
                               boilerplate::parameters::spread)->load()) < 0.01f,
                 "Init preset inherited the live Spread value");
        require (std::abs (parameters.getRawParameterValue (
                               boilerplate::parameters::strum)->load()) < 0.01f,
                 "Init preset inherited the live Strum value");
        require (std::abs (parameters.getRawParameterValue (
                               boilerplate::parameters::chordType)->load()) < 0.01f,
                 "Init preset inherited the live Chord Type");
    }

    void testStateAndProcessing()
    {
        PluginProcessor source;
        setParameter (source, boilerplate::parameters::gain, -24.0f);
        setParameter (source, boilerplate::parameters::bypass, 1.0f);
        setParameter (source, boilerplate::parameters::chordType, 2.0f);
        setParameter (source, boilerplate::parameters::complexity, 6.0f);
        setParameter (source, boilerplate::parameters::voicing, 2.0f);
        setParameter (source, boilerplate::parameters::pitchOffset, 5.0f);
        setParameter (source, boilerplate::parameters::power, 0.0f);
        setParameter (source, boilerplate::parameters::spread, 72.0f);
        setParameter (source, boilerplate::parameters::spreadMode, 1.0f);
        setParameter (source, boilerplate::parameters::humanize, 33.0f);
        setParameter (source, boilerplate::parameters::noteOffMode, 1.0f);
        setParameter (source, boilerplate::parameters::strum, 44.0f);
        setParameter (source, boilerplate::parameters::strumMode, 1.0f);
        juce::MemoryBlock state;
        source.getStateInformation (state);
        require (state.getSize() > 0, "Empty saved state");

        PluginProcessor restored;
        restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        auto& parameters = restored.getValueTreeState();
        require (std::abs (parameters.getRawParameterValue (boilerplate::parameters::gain)->load()
                          + 24.0f) < 0.01f, "Gain recall failed");
        require (parameters.getRawParameterValue (boilerplate::parameters::bypass)->load() > 0.5f,
                 "Bypass recall failed");
        require (std::abs (parameters.getRawParameterValue (boilerplate::parameters::chordType)->load()
                          - 2.0f) < 0.01f, "Chord type recall failed");
        require (std::abs (parameters.getRawParameterValue (boilerplate::parameters::complexity)->load()
                          - 6.0f) < 0.01f, "Complexity recall failed");
        require (std::abs (parameters.getRawParameterValue (boilerplate::parameters::voicing)->load()
                          - 2.0f) < 0.01f, "Voicing recall failed");
        require (std::abs (parameters.getRawParameterValue (boilerplate::parameters::pitchOffset)->load()
                          - 5.0f) < 0.01f, "Pitch offset recall failed");
        require (parameters.getRawParameterValue (boilerplate::parameters::power)->load() < 0.5f,
                 "Power recall failed");
        require (std::abs (parameters.getRawParameterValue (boilerplate::parameters::spread)->load()
                          - 72.0f) < 0.01f, "Spread recall failed");
        require (std::abs (parameters.getRawParameterValue (boilerplate::parameters::spreadMode)->load()
                          - 1.0f) < 0.01f, "Spread mode recall failed");
        require (std::abs (parameters.getRawParameterValue (boilerplate::parameters::humanize)->load()
                          - 33.0f) < 0.01f, "Humanize recall failed");
        require (std::abs (parameters.getRawParameterValue (boilerplate::parameters::noteOffMode)->load()
                          - 1.0f) < 0.01f, "Note-off mode recall failed");
        require (std::abs (parameters.getRawParameterValue (boilerplate::parameters::strum)->load()
                          - 44.0f) < 0.01f, "Strum recall failed");
        require (std::abs (parameters.getRawParameterValue (boilerplate::parameters::strumMode)->load()
                          - 1.0f) < 0.01f, "Strum mode recall failed");
        require (restored.getBypassParameter() == parameters.getParameter (boilerplate::parameters::bypass),
                 "Wrong host bypass parameter");
        require (parameters.getParameter (boilerplate::parameters::gain)->getVersionHint() == 1
                 && restored.getBypassParameter()->getVersionHint() == 1, "Released AU hints changed");
        require (parameters.getParameter (boilerplate::parameters::chordType)->getVersionHint() == 2
                 && parameters.getParameter (boilerplate::parameters::complexity)->getVersionHint() == 3
                 && parameters.getParameter (boilerplate::parameters::voicing)->getVersionHint() == 4
                 && parameters.getParameter (boilerplate::parameters::pitchOffset)->getVersionHint() == 5,
                 "Chord matrix AU hints changed");
        require (parameters.getParameter (boilerplate::parameters::power)->getVersionHint() == 6
                 && parameters.getParameter (boilerplate::parameters::spread)->getVersionHint() == 7
                 && parameters.getParameter (boilerplate::parameters::spreadMode)->getVersionHint() == 8
                 && parameters.getParameter (boilerplate::parameters::humanize)->getVersionHint() == 9
                 && parameters.getParameter (boilerplate::parameters::noteOffMode)->getVersionHint() == 10
                 && parameters.getParameter (boilerplate::parameters::strum)->getVersionHint() == 11
                 && parameters.getParameter (boilerplate::parameters::strumMode)->getVersionHint() == 12,
                 "Interaction engine AU hints changed");

        restored.setStateInformation (nullptr, 0);
        juce::MemoryBlock unrelated;
        {
            juce::MemoryOutputStream stream (unrelated, false);
            juce::ValueTree ("Unrelated").writeToStream (stream);
        }
        restored.setStateInformation (unrelated.getData(), static_cast<int> (unrelated.getSize()));
        require (parameters.getRawParameterValue (boilerplate::parameters::bypass)->load() > 0.5f,
                 "Unrelated state modified parameters");

        require (restored.acceptsMidi(), "Chord plugin must accept MIDI input");
        require (restored.producesMidi(), "Chord plugin must produce MIDI output");
        require (restored.getBusCount (true) == (JucePlugin_IsSynth ? 0 : 1), "Incorrect input buses");
        require (restored.getBusCount (false) == 1, "Incorrect output buses");

        for (const auto channels : { 1, 2 })
        {
            auto layout = restored.getBusesLayout();
            layout.outputBuses.set (0, juce::AudioChannelSet::canonicalChannelSet (channels));
           #if ! JucePlugin_IsSynth
            layout.inputBuses.set (0, layout.outputBuses[0]);
            auto mismatched = layout;
            mismatched.inputBuses.set (0, juce::AudioChannelSet::canonicalChannelSet (3 - channels));
            require (! restored.isBusesLayoutSupported (mismatched), "Accepted mismatched effect buses");
           #else
            auto withInput = layout;
            withInput.inputBuses.add (juce::AudioChannelSet::stereo());
            require (! restored.isBusesLayoutSupported (withInput), "Instrument accepted an input bus");
           #endif
            require (restored.setBusesLayout (layout), "Rejected mono/stereo layout");

            for (const auto sampleRate : { 44100.0, 96000.0 })
            {
                restored.prepareToPlay (sampleRate, 64);
                for (const auto samples : { 0, 1, 17, 64, 1024 })
                {
                    juce::AudioBuffer<float> buffer (channels, samples);
                    fill (buffer, 0.25f);
                    juce::MidiBuffer midi;
                    if (samples > 0)
                        midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), samples - 1);
                    restored.processBlock (buffer, midi);
                    expectSamples (buffer, JucePlugin_IsSynth ? 0.0f : 0.25f);
                }
                restored.releaseResources();
            }
        }
        auto unsupported = restored.getBusesLayout();
        unsupported.outputBuses.set (0, juce::AudioChannelSet::create5point1());
        require (! restored.isBusesLayoutSupported (unsupported), "Accepted surround output");
    }

    void testReviewRegressions()
    {
        using namespace boilerplate::parameters;
        const auto on = [] (int ch, int note) { return juce::MidiMessage::noteOn (ch, note, juce::uint8 (100)); };
        const auto off = [] (int ch, int note) { return juce::MidiMessage::noteOff (ch, note); };
        const auto held = [] (PluginProcessor& p, int note)
        {
            return (p.getActiveOutputNotes (note / 64) & (juce::uint64 (1) << (note % 64))) != 0;
        };
        PluginProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 64);
        p.prepareToPlay (48000.0, 64);
        setChordMatrix (p, 0, 0, 1, 0);
        setParameter (p, strum, 100);
        juce::AudioBuffer<float> audio (2, 64);
        juce::MidiBuffer midi;
        midi.ensureSize (65536);
        midi.addEvent (on (1, 60), 0);
        midi.addEvent (off (1, 60), 32);
        p.processBlock (audio, midi);
        auto events = captureMidi (midi);
        require (hasNoteOn (events, 0, 60) && hasNoteOff (events, 0, 60), "Short strum swallowed its root");
        require (countNoteOns (events) == 1, "Short strum played a future tone");
        require (events[0].position == 0 && events[1].position == 32, "Short strum lost sample offsets");
        for (int i = 0; i < 100; ++i)
            require (processMessages (p, {}).empty(), "Cancelled strum resurrected a note");

        // A scheduled note due before the release in a later block must play.
        p.reset();
        processMessages (p, { on (1, 60) });
        for (int i = 0; i < 44; ++i)
            processMessages (p, {});
        midi.clear();
        midi.addEvent (off (1, 60), 32); // E is due at this block's sample zero.
        p.processBlock (audio, midi);
        events = captureMidi (midi);
        require (hasNoteOn (events, 0, 64) && hasNoteOff (events, 0, 64), "Later release swallowed an already-due tone");

        // Identical input timelines must produce identical MIDI at different
        // host buffer sizes, including note-on and note-off in one large block.
        const auto renderTimeline = [&] (int blockSize)
        {
            PluginProcessor timeline;
            timeline.setRateAndBufferSizeDetails (48000.0, blockSize);
            timeline.prepareToPlay (48000.0, blockSize);
            setChordMatrix (timeline, 0, 0, 1, 0);
            setParameter (timeline, strum, 100);
            std::vector<CapturedEvent> result;
            for (int start = 0; start < 8192; start += blockSize)
            {
                juce::AudioBuffer<float> blockAudio (2, blockSize);
                juce::MidiBuffer input;
                if (start == 0)
                    input.addEvent (on (1, 60), 0);
                if (start <= 2900 && start + blockSize > 2900)
                    input.addEvent (off (1, 60), 2900 - start);
                timeline.processBlock (blockAudio, input);
                for (auto event : captureMidi (input))
                {
                    event.position += start;
                    result.push_back (event);
                }
            }
            return result;
        };
        const auto reference = renderTimeline (64);
        for (const auto blockSize : { 512, 4096 })
        {
            const auto rendered = renderTimeline (blockSize);
            require (rendered.size() == reference.size(), "Strum depends on host block size");
            for (size_t i = 0; i < reference.size(); ++i)
                require (rendered[i].position == reference[i].position
                         && rendered[i].status == reference[i].status
                         && rendered[i].data1 == reference[i].data1,
                         "Strum timeline differs across host buffer sizes");
        }

        // Flush both active and already-retired staggered notes on bypass.
        for (const auto* switchID : { power, bypass })
        {
            p.reset();
            setParameter (p, strum, 0);
            setParameter (p, noteOffMode, 1);
            setParameter (p, power, 1);
            setParameter (p, bypass, 0);
            processMessages (p, { on (1, 60) });
            processMessages (p, { off (1, 60) });
            setParameter (p, switchID, switchID == power ? 0.0f : 1.0f);
            events = processMessages (p, { on (1, 64) });
            require (hasNoteOff (events, 0, 64) && hasNoteOn (events, 0, 64), "Bypass failed to flush old releases first");
            for (int i = 0; i < 100; ++i)
                require (! hasNoteOff (processMessages (p, {}), 0, 64), "Old release cut a held pass-through note");
            setParameter (p, switchID, switchID == power ? 1.0f : 0.0f);
            processMessages (p, { on (1, 60) }); // Generated E shares the raw E.
            events = processMessages (p, { off (1, 64) });
            require (! hasNoteOff (events, 0, 64), "Raw release cut a generated owner");
            setParameter (p, noteOffMode, 0);
            events = processMessages (p, { off (1, 60) });
            require (hasNoteOff (events, 0, 64), "Shared raw/generated ownership leaked a note");
        }

        p.reset();
        processMessages (p, { on (1, 60), on (2, 60) });
        processMessages (p, { off (1, 60) });
        require (held (p, 60) && held (p, 64), "Display lost another channel's held notes");
        processMessages (p, { juce::MidiMessage::allNotesOff (1) });
        require (held (p, 60), "Channel panic cleared another channel's display");
        processMessages (p, { off (2, 60) });
        require (! held (p, 60), "Final channel release failed to clear display");

        // Fresh host buffers must never take away internal scratch storage.
        const auto* storage = PluginProcessorTestAccess::outputStorage (p);
        for (int i = 0; i < 20; ++i)
        {
            processMessages (p, { on (1, 60), off (1, 60) });
            require (PluginProcessorTestAccess::outputStorage (p) == storage, "Internal output storage was exchanged or resized");
        }
        midi.clear();
        const auto* hostStorage = midi.data.begin();
        midi.addEvent (on (1, 60), 0);
        p.processBlock (audio, midi);
        require (midi.data.begin() == hostStorage, "Preallocated host output storage grew");
        processMessages (p, { off (1, 60) });

        // Overload silences the destination instead of dropping an arbitrary release.
        midi.clear();
        for (int i = 0; i < 4097; ++i)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 64), 0);
        p.processBlock (audio, midi);
        events = captureMidi (midi);
        require (events.size() == 32 && p.getNumSoundingChords() == 0, "MIDI overload did not panic cleanly");
        for (int ch = 0; ch < 16; ++ch)
            require (hasEvent (events, 0xb0, ch, 120, 0) && hasEvent (events, 0xb0, ch, 123, 0), "Overload missed a channel");
        midi.clear();
        std::vector<juce::uint8> largeSysEx (65530, 1);
        largeSysEx.front() = 0xf0;
        largeSysEx.back() = 0xf7;
        midi.addEvent (largeSysEx.data(), static_cast<int> (largeSysEx.size()), 0);
        p.processBlock (audio, midi);
        require (captureMidi (midi).size() == 32, "Oversized SysEx bypassed the byte bound");
        for (int i = 0; i < 300; ++i)
            p.queueKeyboardNote ((i % 2) == 0, 60, 0.8f);
        midi.clear();
        p.processBlock (audio, midi);
        require (captureMidi (midi).size() == 32 && p.getNumSoundingChords() == 0,
                 "Keyboard FIFO overflow leaked a note");
    }

    void testExpandedFeatures()
    {
        using namespace boilerplate::parameters;
        PluginProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 64);
        p.prepareToPlay (48000.0, 64);
        const auto on = [] (int note) { return juce::MidiMessage::noteOn (1, note, juce::uint8 (100)); };
        const auto off = [] (int note) { return juce::MidiMessage::noteOff (1, note); };
        setParameter (p, voicingStyle, 1);
        setParameter (p, voicing, 0);
        auto events = processMessages (p, { on (60) });
        require (hasNoteOn (events, 0, 64) && hasNoteOn (events, 0, 67) && hasNoteOn (events, 0, 72), "High is not a first inversion");
        processMessages (p, { off (60) });
        setParameter (p, voicing, 2);
        events = processMessages (p, { on (60) });
        require (hasNoteOn (events, 0, 55) && hasNoteOn (events, 0, 60) && hasNoteOn (events, 0, 64), "Low is not a lower inversion");
        processMessages (p, { off (60) });
        setParameter (p, voicing, 1);
        for (const auto style : { 2, 3 })
        {
            p.reset();
            setParameter (p, voicingStyle, static_cast<float> (style));
            bool sawDifferent = false;
            for (int i = 0; i < 20; ++i)
            {
                events = processMessages (p, { on (60) });
                sawDifferent = sawDifferent || ! hasNoteOn (events, 0, 60);
                const auto released = processMessages (p, { off (60) });
                require (countNoteOns (events) == 3 && released.size() == 3, "Automatic inversion leaked notes");
            }
            if (style == 2)
                require (sawDifferent, "Random voicing never chose another inversion");
        }
        p.reset();
        setParameter (p, voicingStyle, 3);
        processMessages (p, { on (60) });
        processMessages (p, { off (60) });
        events = processMessages (p, { on (67) });
        require (hasNoteOn (events, 0, 59) && hasNoteOn (events, 0, 62) && hasNoteOn (events, 0, 67), "Smart voicing did not minimize movement");
        processMessages (p, { off (67) });
        setParameter (p, voicingStyle, 1);
        setParameter (p, scaleRoot, 2);
        setParameter (p, scaleMode, 2);
        events = processMessages (p, { on (60) });
        require (hasNoteOn (events, 0, 62) && hasNoteOn (events, 0, 65) && hasNoteOn (events, 0, 69), "D-minor scale transformation is incorrect");
        processMessages (p, { off (60) });
        setParameter (p, scaleRoot, 0);
        setParameter (p, scaleMode, 0);

        p.setPitchClassRecipe (0, { 1, 0, 1, 0 });
        for (const int note : { 48, 60, 72 })
        {
            events = processMessages (p, { on (note) });
            require (hasNoteOn (events, 0, note + 3) && ! hasNoteOn (events, 0, note + 4), "Pitch-class mapping did not apply across octaves");
            processMessages (p, { off (note) });
        }
        p.setKeyRecipe (60, { 0, 0, 1, 0 });
        events = processMessages (p, { on (60) });
        require (hasNoteOn (events, 0, 64), "Exact-note override did not take precedence");
        processMessages (p, { off (60) });
        p.clearKeyRecipe (60);
        p.clearPitchClassRecipe (0);

        setParameter (p, humanize, 100);
        setParameter (p, humanizeTiming, 1);
        std::vector<CapturedEvent> timed;
        auto block = processMessages (p, { on (60) });
        timed.insert (timed.end(), block.begin(), block.end());
        for (int i = 1; i < 10; ++i)
        {
            block = processMessages (p, {});
            for (auto& event : block)
                event.position += i * 64;
            timed.insert (timed.end(), block.begin(), block.end());
        }
        require (countNoteOns (timed) == 3, "Humanize timing dropped notes");
        bool delayed = false;
        for (const auto& event : timed)
        {
            require (event.position >= 0 && event.position <= 480, "Humanize exceeded 10 ms");
            delayed = delayed || event.position > 0;
        }
        require (delayed, "Humanize timing never delayed a note");
        processMessages (p, { off (60) });

        // Session recall includes scale and both mapping layers.
        setParameter (p, humanize, 0);
        p.setPitchClassRecipe (0, { 1, 2, 1, 0 });
        p.setKeyRecipe (60, { 2, 2, 1, 0 });
        setParameter (p, scaleRoot, 5);
        setParameter (p, scaleMode, 2);
        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        PluginProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        require (restored.hasPitchClassRecipe (0) && restored.hasKeyRecipe (60), "Mapping layers were not recalled");
        require (juce::roundToInt (restored.getValueTreeState().getRawParameterValue (scaleRoot)->load()) == 5, "Session scale was not recalled");

        // User presets capture maps but preserve the destination's scale.
        juce::TemporaryFile preset (".wickychord");
        wicky::ui::PresetManager bank (p);
        require (bank.saveUserPreset (preset.getFile()), "User preset save failed");
        p.clearKeyRecipe (60);
        p.clearPitchClassRecipe (0);
        setParameter (p, scaleRoot, 7);
        setParameter (p, scaleMode, 1);
        require (bank.loadUserPreset (preset.getFile()), "User preset recall failed");
        require (p.hasPitchClassRecipe (0) && p.hasKeyRecipe (60), "User preset lost mappings");
        require (juce::roundToInt (p.getValueTreeState().getRawParameterValue (scaleRoot)->load()) == 7
                 && juce::roundToInt (p.getValueTreeState().getRawParameterValue (scaleMode)->load()) == 1, "Preset changed the selected scale");
        require (bank.loadPreset (0), "Factory Init failed");
        require (juce::roundToInt (p.getValueTreeState().getRawParameterValue (scaleRoot)->load()) == 7, "Factory preset changed scale");

        auto legacy = juce::ValueTree::readFromData (saved.getData(), saved.getSize());
        for (int i = legacy.getNumChildren(); --i >= 0;)
        {
            const auto id = legacy.getChild (i).getProperty ("id").toString();
            if (id == voicingStyle || id == humanizeTiming || id == scaleRoot || id == scaleMode)
                legacy.removeChild (i, nullptr);
        }
        juce::MemoryBlock oldState;
        juce::MemoryOutputStream stream (oldState, false);
        legacy.writeToStream (stream);
        restored.setStateInformation (oldState.getData(), static_cast<int> (oldState.getSize()));
        require (juce::roundToInt (restored.getValueTreeState().getRawParameterValue (voicingStyle)->load()) == 0
                 && juce::roundToInt (restored.getValueTreeState().getRawParameterValue (humanizeTiming)->load()) == 0,
                 "Legacy state changed its sound contract");
        juce::TemporaryFile oldPreset (".wickychord");
        require (oldPreset.getFile().replaceWithData (oldState.getData(), oldState.getSize()), "Could not write legacy preset fixture");
        require (bank.loadUserPreset (oldPreset.getFile()), "Legacy preset import failed");
        require (juce::roundToInt (p.getValueTreeState().getRawParameterValue (scaleRoot)->load()) == 7,
                 "Legacy preset import changed the selected scale");
        juce::TemporaryFile invalid (".wickychord");
        require (invalid.getFile().replaceWithText ("invalid"), "Could not write invalid preset fixture");
        require (! bank.loadUserPreset (invalid.getFile()), "Invalid user preset was accepted");
        for (const auto* id : { voicingStyle, humanizeTiming, scaleRoot, scaleMode })
            require (p.getValueTreeState().getParameter (id)->getVersionHint() > 12, "New AU hint reused a released value");
    }

    void testEditor()
    {
        PluginProcessor processor;
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
            require (editor != nullptr, "Editor creation failed");
            require (editor->getAudioProcessor() == &processor, "Editor has the wrong processor");
            editor->setSize (780, 510);
            juce::Image image (juce::Image::ARGB, 780, 510, true);
            juce::Graphics graphics (image);
            editor->paintEntireComponent (graphics, true);
            processor.editorBeingDeleted (editor.get());
            editor.reset();
            require (processor.getActiveEditor() == nullptr, "Editor lifetime was not cleared");
        }
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    try
    {
        if (argc > 1 && std::string (argv[1]) == "--editor")
            testEditor();
        else
        {
            testGain();
            testOxaniumFont();
            testMidiVelocityGain();
            testChordEngine();
            testPerKeyRecipes();
            testVirtualKeyboardBridge();
            testInteractionEngine();
            testPresetManager();
            testReviewRegressions();
            testExpandedFeatures();
            testStateAndProcessing();
        }
        std::cout << "Passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
