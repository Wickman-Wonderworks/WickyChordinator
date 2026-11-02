#include "PluginProcessor.h"
#include "DSP/GainProcessor.h"
#include "State/ParameterIDs.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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
        expectSamples (buffer, 0.25f);

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

    void testStateAndProcessing()
    {
        PluginProcessor source;
        setParameter (source, boilerplate::parameters::gain, -24.0f);
        setParameter (source, boilerplate::parameters::bypass, 1.0f);
        setParameter (source, boilerplate::parameters::chordType, 2.0f);
        setParameter (source, boilerplate::parameters::complexity, 6.0f);
        setParameter (source, boilerplate::parameters::voicing, 2.0f);
        setParameter (source, boilerplate::parameters::pitchOffset, 5.0f);
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
        require (restored.getBypassParameter() == parameters.getParameter (boilerplate::parameters::bypass),
                 "Wrong host bypass parameter");
        require (parameters.getParameter (boilerplate::parameters::gain)->getVersionHint() == 1
                 && restored.getBypassParameter()->getVersionHint() == 1, "Released AU hints changed");
        require (parameters.getParameter (boilerplate::parameters::chordType)->getVersionHint() == 2
                 && parameters.getParameter (boilerplate::parameters::complexity)->getVersionHint() == 3
                 && parameters.getParameter (boilerplate::parameters::voicing)->getVersionHint() == 4
                 && parameters.getParameter (boilerplate::parameters::pitchOffset)->getVersionHint() == 5,
                 "Chord matrix AU hints changed");

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
            testMidiVelocityGain();
            testChordEngine();
            testPerKeyRecipes();
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
