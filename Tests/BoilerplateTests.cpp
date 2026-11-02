#include "PluginProcessor.h"
#include "DSP/GainProcessor.h"
#include "State/ParameterIDs.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

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

    int firstNoteOnVelocity (const juce::MidiBuffer& midi)
    {
        for (const auto metadata : midi)
            if (metadata.numBytes == 3
                && (metadata.data[0] & 0xf0) == 0x90
                && metadata.data[2] > 0)
                return metadata.data[2];

        return -1;
    }

    void testMidiVelocityGain()
    {
        PluginProcessor processor;
        processor.prepareToPlay (48000.0, 64);

        auto processVelocity = [&] (float gainDb, bool bypassed, int velocity)
        {
            setParameter (processor, boilerplate::parameters::gain, gainDb);
            setParameter (processor, boilerplate::parameters::bypass, bypassed ? 1.0f : 0.0f);

            juce::AudioBuffer<float> buffer (2, 64);
            fill (buffer, 0.25f);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (velocity)), 0);
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 64), 1);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 2);

            processor.processBlock (buffer, midi);

            int eventCount = 0;
            int controllerValue = -1;
            bool sawNoteOff = false;

            for (const auto metadata : midi)
            {
                ++eventCount;
                const auto message = metadata.getMessage();

                if (message.isNoteOff (false))
                    sawNoteOff = true;
                else if (message.isController())
                    controllerValue = message.getControllerValue();
            }

            require (eventCount == 3, "MIDI events were lost");
            require (sawNoteOff, "Note-off was lost");
            require (controllerValue == 64, "Controller data changed");
            expectSamples (buffer, 0.25f); // Audio stays a transparent pass-through.

            return firstNoteOnVelocity (midi);
        };

        require (processVelocity (0.0f, false, 100) == 100, "0 dB changed velocity");
        require (processVelocity (-6.0f, false, 100) == 50, "Velocity attenuation failed");
        require (processVelocity (12.0f, false, 100) == 127, "Velocity boost did not clamp");
        require (processVelocity (-60.0f, false, 100) == 1, "Full attenuation lost the note-on");
        require (processVelocity (-24.0f, true, 100) == 100, "Bypass changed velocity");
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
