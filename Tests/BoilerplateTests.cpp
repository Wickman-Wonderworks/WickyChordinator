#include "PluginProcessor.h"
#include "ChordSettingsPanel.h"
#include "DSP/GainProcessor.h"
#include "DSP/StrumOrder.h"
#include "DSP/StrumTiming.h"
#include "Standalone/MidiRouting.h"
#include "Standalone/MidiRoutingPanel.h"
#include "State/ParameterIDs.h"
#include "UI/OxaniumFonts.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
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

    static void setHumanizeSeed (PluginProcessor& processor, juce::uint32 seed)
    {
        processor.humanizeSeed.store (seed, std::memory_order_relaxed);
    }
};

namespace
{
    void require (bool condition, const char* message)
    {
        if (! condition)
            throw std::runtime_error (message);
    }

    juce::Component* findComponentByName (juce::Component& root, const juce::String& name)
    {
        if (root.getName() == name)
            return &root;

        for (int i = 0; i < root.getNumChildComponents(); ++i)
            if (auto* child = root.getChildComponent (i))
                if (auto* found = findComponentByName (*child, name))
                    return found;

        return nullptr;
    }

    //==========================================================================
    struct FakeMidiBackend final : public wicky::standalone::MidiRoutingBackend
    {
        wicky::standalone::MidiDeviceList inputs;
        wicky::standalone::MidiDeviceList outputs;
        juce::StringArray openInputIds;
        juce::String openOutputId;
        bool outputOpen = false;
        bool failInputOpen = false;
        bool failOutputOpen = false;
        bool failSends = false;
        juce::Array<juce::MidiMessage> sent;
        int inputEnumerationCount = 0;
        int outputEnumerationCount = 0;
        juce::MidiInputCallback* inputCallback = nullptr;

        static bool contains (const wicky::standalone::MidiDeviceList& list,
                              const juce::String& identifier)
        {
            for (const auto& device : list)
                if (device.identifier == identifier)
                    return true;

            return false;
        }

        wicky::standalone::MidiDeviceList getInputDevices() override
        {
            ++inputEnumerationCount;
            return inputs;
        }

        wicky::standalone::MidiDeviceList getOutputDevices() override
        {
            ++outputEnumerationCount;
            return outputs;
        }

        bool openInput (const juce::String& identifier,
                        juce::MidiInputCallback* callback) override
        {
            if (failInputOpen || ! contains (inputs, identifier))
                return false;

            inputCallback = callback;
            openInputIds.addIfNotAlreadyThere (identifier);
            return true;
        }

        void closeInputs() override
        {
            openInputIds.clear();
            inputCallback = nullptr;
        }

        juce::StringArray getOpenInputIdentifiers() const override
        {
            return openInputIds;
        }

        bool openOutput (const juce::String& identifier) override
        {
            if (failOutputOpen || ! contains (outputs, identifier))
                return false;

            openOutputId = identifier;
            outputOpen = true;
            return true;
        }

        void closeOutput() override
        {
            openOutputId.clear();
            outputOpen = false;
        }

        bool send (const juce::MidiMessage& message) override
        {
            if (! outputOpen || failSends)
                return false;

            sent.add (message);
            return true;
        }

        bool isOutputOpen() const override
        {
            return outputOpen;
        }

        juce::String getOpenOutputIdentifier() const override
        {
            return openOutputId;
        }

        void injectInput (const juce::MidiMessage& message)
        {
            if (inputCallback != nullptr)
                inputCallback->handleIncomingMidiMessage (nullptr, message);
        }
    };

    struct FakeRoutingStore final : public wicky::standalone::MidiRoutingSettingsStore
    {
        juce::PropertySet values;
        int saveCount = 0;

        juce::String getValue (const juce::String& key) const override
        {
            return values.getValue (key);
        }

        bool containsKey (const juce::String& key) const override
        {
            return values.containsKey (key);
        }

        void setValue (const juce::String& key, const juce::String& value) override
        {
            values.setValue (key, value);
        }

        void saveIfNeeded() override
        {
            ++saveCount;
        }
    };

    class FakeMidiSourceProcessor final : public juce::AudioProcessor
    {
    public:
        struct Pending
        {
            juce::MidiMessage message;
            int position = 0;
        };

        FakeMidiSourceProcessor()
            : juce::AudioProcessor (BusesProperties()
                  .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
        {
        }

        void prepareToPlay (double, int) override {}
        void releaseResources() override {}

        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer& midi) override
        {
            for (const auto& event : pending)
                midi.addEvent (event.message, event.position);

            pending.clear();
        }

        const juce::String getName() const override { return "Fake MIDI Source"; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return true; }
        double getTailLengthSeconds() const override { return 0.0; }
        bool hasEditor() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}

        std::vector<Pending> pending;
    };

    class FakeMidiRoutingPanelModel final : public wicky::standalone::MidiRoutingPanelModel
    {
    public:
        wicky::standalone::MidiDeviceList inputs;
        wicky::standalone::MidiDeviceList outputs;
        wicky::standalone::MidiRoutingSettings settings;
        juce::StringArray enabledInputs;
        int refreshCount = 0;
        int panicCount = 0;
        int lastChannel = -1;
        juce::String lastOutput;
        bool outputOpen = true;

        wicky::standalone::MidiDeviceList getInputDevices() override { return inputs; }
        wicky::standalone::MidiDeviceList getOutputDevices() override { return outputs; }
        wicky::standalone::MidiRoutingSettings getSettings() const override { return settings; }

        bool isInputEnabled (const juce::String& identifier) const override
        {
            return enabledInputs.contains (identifier);
        }

        void setInputEnabled (const juce::String& identifier, bool enabled) override
        {
            if (enabled)
            {
                enabledInputs.addIfNotAlreadyThere (identifier);
                settings.inputIdentifiers.addIfNotAlreadyThere (identifier);
            }
            else
            {
                enabledInputs.removeString (identifier);
                settings.inputIdentifiers.removeString (identifier);
            }
        }

        bool setOutput (const juce::String& identifier) override
        {
            lastOutput = identifier;
            settings.outputIdentifier = identifier;
            return outputOpen;
        }

        void setOutputChannel (int channel) override
        {
            lastChannel = channel;
            settings.outputChannel = channel;
        }

        void refresh() override { ++refreshCount; }
        void sendNoteOffs() override { ++panicCount; }

        wicky::standalone::MidiRouting::Status getStatus() const override
        {
            wicky::standalone::MidiRouting::Status status;
            status.outputIdentifier = settings.outputIdentifier;
            status.outputSelected = settings.outputIdentifier.isNotEmpty();
            status.outputConnected = outputOpen && status.outputSelected;
            status.outputName = "Output A";
            status.selectedInputIdentifiers = settings.inputIdentifiers;

            for (const auto& identifier : settings.inputIdentifiers)
                if (isInputEnabled (identifier))
                    status.connectedInputIdentifiers.add (identifier);

            return status;
        }
    };

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

    /** Controllable host playhead for tempo and transport-position tests. */
    class TestPlayHead final : public juce::AudioPlayHead
    {
    public:
        juce::Optional<PositionInfo> getPosition() const override
        {
            if (! positionAvailable)
                return {};

            PositionInfo info;

            if (tempoAvailable)
                info.setBpm (tempoBpm);

            info.setPpqPosition (ppqPosition);
            info.setTimeInSamples (timeInSamples);
            info.setIsPlaying (playing);
            return info;
        }

        bool positionAvailable = true;
        bool tempoAvailable = true;
        double tempoBpm = 120.0;
        double ppqPosition = 0.0;
        juce::int64 timeInSamples = 0;
        bool playing = false;
    };

    struct TempoScenario
    {
        int blockSize = 64;
        int totalSamples = 24000;
        int chordType = 0;
        int complexity = 0;
        int voicing = 1;
        int pitchOffset = 0;
        int division = 3;
        int fallbackBpm = 120;
        int noteOffMode = 0;
        int strumMode = 0;
        int pattern = 0;
        juce::uint32 randomSeed = 0;
        bool sync = true;
        bool attachPlayHead = true;
        bool positionAvailable = true;
        bool tempoAvailable = true;
        bool playing = false;
        double initialBpm = 120.0;
        float strumPercent = 0.0f;
        int positionJumpAt = -1;
        double positionJumpPpq = 1000.0;
        std::vector<std::pair<int, juce::MidiMessage>> input;
        std::vector<std::pair<int, double>> tempoChanges; // absolute sample -> BPM
    };

    std::vector<CapturedEvent> renderScenario (const TempoScenario& scenario,
                                               std::vector<CapturedEvent>* firstBlock = nullptr)
    {
        TestPlayHead playHead;
        playHead.positionAvailable = scenario.positionAvailable;
        playHead.tempoAvailable = scenario.tempoAvailable;
        playHead.tempoBpm = scenario.initialBpm;
        playHead.playing = scenario.playing;

        PluginProcessor processor;

        if (scenario.attachPlayHead)
            processor.setPlayHead (&playHead);

        processor.setRateAndBufferSizeDetails (48000.0, scenario.blockSize);
        processor.prepareToPlay (48000.0, scenario.blockSize);

        if (scenario.randomSeed != 0)
            PluginProcessorTestAccess::setHumanizeSeed (processor, scenario.randomSeed);

        setChordMatrix (processor, scenario.chordType, scenario.complexity,
                        scenario.voicing, scenario.pitchOffset);
        setParameter (processor, boilerplate::parameters::strum, scenario.strumPercent);
        setParameter (processor, boilerplate::parameters::strumMode,
                      static_cast<float> (scenario.strumMode));
        setParameter (processor, boilerplate::parameters::strumTimingMode,
                      scenario.sync ? 1.0f : 0.0f);
        setParameter (processor, boilerplate::parameters::strumDivision,
                      static_cast<float> (scenario.division));
        setParameter (processor, boilerplate::parameters::strumFallbackBpm,
                      static_cast<float> (scenario.fallbackBpm));
        setParameter (processor, boilerplate::parameters::strumPattern,
                      static_cast<float> (scenario.pattern));
        setParameter (processor, boilerplate::parameters::noteOffMode,
                      static_cast<float> (scenario.noteOffMode));

        std::vector<CapturedEvent> result;
        size_t nextChange = 0;

        for (int start = 0; start < scenario.totalSamples; start += scenario.blockSize)
        {
            // A host tempo change is visible from the next block boundary. The
            // processor reads the playhead once at the start of each block.
            while (nextChange < scenario.tempoChanges.size()
                   && scenario.tempoChanges[nextChange].first <= start)
            {
                playHead.tempoAvailable = true;
                playHead.tempoBpm = scenario.tempoChanges[nextChange].second;
                ++nextChange;
            }

            if (scenario.positionJumpAt >= start
                && scenario.positionJumpAt < start + scenario.blockSize)
            {
                playHead.ppqPosition = scenario.positionJumpPpq;
                playHead.timeInSamples = 123456789;
            }

            juce::AudioBuffer<float> audio (2, scenario.blockSize);
            juce::MidiBuffer midi;

            for (const auto& event : scenario.input)
                if (event.first >= start && event.first < start + scenario.blockSize)
                    midi.addEvent (event.second, event.first - start);

            processor.processBlock (audio, midi);
            auto events = captureMidi (midi);

            if (start == 0 && firstBlock != nullptr)
                *firstBlock = events;

            for (auto& event : events)
            {
                event.position += start;
                result.push_back (event);
            }
        }

        return result;
    }

    std::vector<int> noteOnPositions (const std::vector<CapturedEvent>& events)
    {
        std::vector<int> positions;

        for (const auto& event : events)
            if (event.status == 0x90 && event.data2 > 0)
                positions.push_back (event.position);

        return positions;
    }

    std::vector<int> noteOnNotes (const std::vector<CapturedEvent>& events,
                                  int channel = -1)
    {
        std::vector<int> notes;

        for (const auto& event : events)
            if (event.status == 0x90 && event.data2 > 0
                && (channel < 0 || event.channel == channel))
                notes.push_back (event.data1);

        return notes;
    }

    std::vector<int> noteOffPositions (const std::vector<CapturedEvent>& events)
    {
        std::vector<int> positions;

        for (const auto& event : events)
            if (event.status == 0x80 || (event.status == 0x90 && event.data2 == 0))
                positions.push_back (event.position);

        return positions;
    }

    bool sameTimeline (const std::vector<CapturedEvent>& a,
                       const std::vector<CapturedEvent>& b)
    {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].position != b[i].position
                || a[i].status != b[i].status
                || a[i].channel != b[i].channel
                || a[i].data1 != b[i].data1
                || a[i].data2 != b[i].data2)
                return false;

        return true;
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

        // Factory presets are built from every parameter's default, so loading
        // one must also reset the tempo-synced strum controls.
        setParameter (processor, boilerplate::parameters::strumTimingMode, 1.0f);
        setParameter (processor, boilerplate::parameters::strumDivision, 4.0f);
        setParameter (processor, boilerplate::parameters::strumFallbackBpm, 90.0f);
        setParameter (processor, boilerplate::parameters::strumPattern, 3.0f);

        require (presets.loadPreset (1), "Could not load the second preset");
        require (presets.getCurrentIndex() == 1, "Preset index did not advance");

        auto& parameters = processor.getValueTreeState();
        require (std::abs (parameters.getRawParameterValue (
                               boilerplate::parameters::spread)->load() - 32.0f) < 0.01f,
                 "Preset did not apply Spread");
        require (std::abs (parameters.getRawParameterValue (
                               boilerplate::parameters::strum)->load() - 22.0f) < 0.01f,
                 "Preset did not apply Strum");
        require (juce::roundToInt (parameters.getRawParameterValue (
                                    boilerplate::parameters::strumTimingMode)->load()) == 0
                     && juce::roundToInt (parameters.getRawParameterValue (
                                    boilerplate::parameters::strumDivision)->load()) == 3
                     && juce::roundToInt (parameters.getRawParameterValue (
                                    boilerplate::parameters::strumFallbackBpm)->load()) == 120
                     && juce::roundToInt (parameters.getRawParameterValue (
                                    boilerplate::parameters::strumPattern)->load()) == 0,
                 "Factory preset did not restore the timing defaults");

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

    void testStrumTiming()
    {
        namespace strum = boilerplate::dsp::strum;

        // The division table is a shipped state contract: three straight bases,
        // each followed by dotted and triplet forms, in this exact order.
        require (strum::numDivisions == 9, "Unexpected strum division count");

        const double bases[] = { 0.5, 0.25, 0.125 };

        for (int group = 0; group < 3; ++group)
        {
            const auto offset = group * 3;
            const auto base = bases[group];

            require (std::abs (strum::divisions[offset].quarterNoteBeats - base) < 1.0e-12,
                     "Straight division value is wrong");
            require (std::abs (strum::divisions[offset + 1].quarterNoteBeats - base * 1.5) < 1.0e-12,
                     "Dotted division value is wrong");
            require (std::abs (strum::divisions[offset + 2].quarterNoteBeats - base * 2.0 / 3.0) < 1.0e-12,
                     "Triplet division value is wrong");
        }

        require (strum::defaultDivisionIndex == 3, "Default division is not the sixteenth note");
        require (std::abs (strum::divisions[strum::defaultDivisionIndex].quarterNoteBeats - 0.25) < 1.0e-12,
                 "Default division is not a quarter-note beat value");

        // Acceptance values at 48 kHz / 120 BPM.
        require (strum::syncStepSamples (48000.0, 120.0, 3) == 6000, "Sixteenth note step is wrong");
        require (strum::syncStepSamples (48000.0, 120.0, 0) == 12000, "Eighth note step is wrong");
        require (strum::syncStepSamples (48000.0, 120.0, 4) == 9000, "Dotted sixteenth step is wrong");
        require (strum::syncStepSamples (48000.0, 120.0, 5) == 4000, "Triplet sixteenth step is wrong");

        // The pure conversion matches the documented formula at every division,
        // sample rate and ordinary tempo.
        const double rates[] = { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
        const auto expectedStep = [] (double rate, double bpm, double beats)
        {
            return juce::roundToInt (60.0 / bpm * beats * rate);
        };

        for (const auto rate : rates)
            for (const auto bpm : { 40.0, 90.0, 120.0, 200.0 })
                for (int index = 0; index < strum::numDivisions; ++index)
                    require (strum::syncStepSamples (rate, bpm, index)
                                 == expectedStep (rate, bpm,
                                                  strum::divisions[index].quarterNoteBeats),
                             "Synced step does not match the documented formula");

        // Sample-rate scaling for the default division at 120 BPM.
        require (strum::syncStepSamples (44100.0, 120.0, 3) == 5512,
                 "44.1 kHz sixteenth rounding changed");
        require (strum::syncStepSamples (96000.0, 120.0, 3) == 12000,
                 "96 kHz sixteenth step is wrong");
        require (strum::syncStepSamples (192000.0, 120.0, 3) == 24000,
                 "192 kHz sixteenth step is wrong");

        // Absent, zero and non-finite tempos use the 120 BPM fallback.
        const auto fallback = strum::syncStepSamples (48000.0, strum::fallbackBpm, 3);
        const double invalidTempos[] =
        {
            0.0,
            -120.0,
            std::nan (""),
            std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity()
        };

        for (const auto bpm : invalidTempos)
            require (strum::syncStepSamples (48000.0, bpm, 3) == fallback,
                     "Invalid tempo did not use the 120 BPM fallback");

        // Host tempo bounds are separate from the 20-300 BPM manual range.
        require (strum::syncStepSamples (48000.0, 0.5, 3) == 480000,
                 "Very slow host tempo was not clamped to the maximum step");
        require (strum::syncStepSamples (48000.0, 1.0e9, 3) == 720,
                 "Very fast host tempo was not clamped to the host maximum");

        // Invalid sample clocks produce no delay instead of undefined conversion.
        const double invalidRates[] =
        {
            0.0,
            -48000.0,
            std::nan (""),
            std::numeric_limits<double>::infinity()
        };

        for (const auto rate : invalidRates)
        {
            require (strum::syncStepSamples (rate, 120.0, 3) == 0,
                     "Invalid sample rate produced a synced step");
            require (strum::freeStepSamples (rate, 100.0f) == 0,
                     "Invalid sample rate produced a free-time step");
        }

        // Out-of-range division indices stay inside the frozen table.
        require (strum::syncStepSamples (48000.0, 120.0, -5)
                     == strum::syncStepSamples (48000.0, 120.0, 0),
                 "Low division index was not clamped");
        require (strum::syncStepSamples (48000.0, 120.0, 99)
                     == strum::syncStepSamples (48000.0, 120.0, strum::numDivisions - 1),
                 "High division index was not clamped");

        // Free mode must remain bit-identical to the released Strum formula.
        for (const auto rate : rates)
            for (int percent = 0; percent <= 100; ++percent)
            {
                const auto legacy = juce::roundToInt (
                    (static_cast<float> (percent) / 100.0f) * 0.060f * rate);
                require (strum::freeStepSamples (rate, static_cast<float> (percent)) == legacy,
                         "Free-time strum mapping changed");
            }

        require (strum::freeStepSamples (48000.0, 100.0f) == 2880,
                 "Free-time maximum changed");
        require (strum::freeStepSamples (48000.0, 0.0f) == 0,
                 "Free-time zero changed");
        require (strum::freeStepSamples (48000.0, 500.0f) == 2880,
                 "Free-time maximum did not clamp");
        require (strum::freeStepSamples (48000.0, -10.0f) == 0,
                 "Free-time minimum did not clamp");

        // The mode dispatcher keeps the two mappings separate.
        require (strum::stepSamples (false, 48000.0, 100.0f, 120.0, 3) == 2880,
                 "Free dispatch used the synced division");
        require (strum::stepSamples (true, 48000.0, 100.0f, 120.0, 3) == 6000,
                 "Sync dispatch used the free-time percentage");
    }

    void testStrumTimingParameters()
    {
        namespace strum = boilerplate::dsp::strum;
        using namespace boilerplate::parameters;

        PluginProcessor processor;
        auto& state = processor.getValueTreeState();

        auto* timing = dynamic_cast<juce::AudioParameterChoice*> (
            state.getParameter (strumTimingMode));
        auto* division = dynamic_cast<juce::AudioParameterChoice*> (
            state.getParameter (strumDivision));
        auto* fallback = dynamic_cast<juce::AudioParameterInt*> (
            state.getParameter (strumFallbackBpm));

        require (timing != nullptr && division != nullptr && fallback != nullptr,
                 "Tempo-synced strum parameters are missing");

        require (timing->choices == juce::StringArray { "Free", "Sync" },
                 "Timing mode choices are wrong");
        require (division->choices == strum::divisionNames(),
                 "Division choices do not match the frozen table");
        require (timing->getIndex() == 0, "Timing mode does not default to Free");
        require (division->getIndex() == strum::defaultDivisionIndex,
                 "Division does not default to the sixteenth note");
        require (fallback->get() == strumFallbackBpmDefault,
                 "Fallback tempo does not default to 120 BPM");
        require (fallback->getRange() == juce::Range<int> { strumFallbackBpmMin, strumFallbackBpmMax },
                 "Fallback tempo range is wrong");

        require (timing->getVersionHint() == 17
                     && division->getVersionHint() == 18
                     && fallback->getVersionHint() == 19,
                 "Tempo-synced strum AU hints are wrong");

        // Non-default values survive a full session round-trip.
        setParameter (processor, strumTimingMode, 1.0f);
        setParameter (processor, strumDivision, 4.0f);
        setParameter (processor, strumFallbackBpm, 90.0f);

        juce::MemoryBlock saved;
        processor.getStateInformation (saved);

        PluginProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        auto& restoredState = restored.getValueTreeState();

        require (juce::roundToInt (restoredState.getRawParameterValue (strumTimingMode)->load()) == 1
                     && juce::roundToInt (restoredState.getRawParameterValue (strumDivision)->load()) == 4
                     && juce::roundToInt (restoredState.getRawParameterValue (strumFallbackBpm)->load()) == 90,
                 "Tempo-synced strum session recall failed");

        // A state written before these controls existed must recall the same
        // Free/sixteenth/120 defaults, even when the destination instance was
        // changed first. This keeps old sessions sounding unchanged.
        auto legacy = juce::ValueTree::readFromData (saved.getData(), saved.getSize());

        for (int i = legacy.getNumChildren(); --i >= 0;)
        {
            const auto id = legacy.getChild (i).getProperty ("id").toString();
            if (id == strumTimingMode || id == strumDivision || id == strumFallbackBpm)
                legacy.removeChild (i, nullptr);
        }

        juce::MemoryBlock legacyState;
        {
            juce::MemoryOutputStream stream (legacyState, false);
            legacy.writeToStream (stream);
        }

        PluginProcessor legacyTarget;
        setParameter (legacyTarget, strumTimingMode, 1.0f);
        setParameter (legacyTarget, strumDivision, 0.0f);
        setParameter (legacyTarget, strumFallbackBpm, 240.0f);
        legacyTarget.setStateInformation (legacyState.getData(),
                                          static_cast<int> (legacyState.getSize()));

        auto& legacyParameters = legacyTarget.getValueTreeState();
        require (juce::roundToInt (legacyParameters.getRawParameterValue (strumTimingMode)->load()) == 0
                     && juce::roundToInt (legacyParameters.getRawParameterValue (strumDivision)->load())
                            == strum::defaultDivisionIndex
                     && juce::roundToInt (legacyParameters.getRawParameterValue (strumFallbackBpm)->load())
                            == strumFallbackBpmDefault,
                 "Legacy state did not recall the free-time defaults");

        // User presets round-trip the timing controls through the normal state.
        juce::TemporaryFile timingPreset (".wickychord");
        wicky::ui::PresetManager timingBank (processor);
        require (timingBank.saveUserPreset (timingPreset.getFile()),
                 "User timing preset save failed");

        setParameter (processor, strumTimingMode, 0.0f);
        setParameter (processor, strumDivision, 0.0f);
        setParameter (processor, strumFallbackBpm, 240.0f);

        require (timingBank.loadUserPreset (timingPreset.getFile()),
                 "User timing preset recall failed");

        auto& timingParameters = processor.getValueTreeState();
        require (juce::roundToInt (timingParameters.getRawParameterValue (strumTimingMode)->load()) == 1
                     && juce::roundToInt (timingParameters.getRawParameterValue (strumDivision)->load()) == 4
                     && juce::roundToInt (timingParameters.getRawParameterValue (strumFallbackBpm)->load()) == 90,
                 "User preset did not recall the timing controls");
    }

    void testStrumOrder()
    {
        namespace strum = boilerplate::dsp::strum;

        const auto isPermutation = [] (const int* order, int size)
        {
            bool seen[strum::maxChordTones] {};

            for (int i = 0; i < size; ++i)
            {
                if (order[i] < 0 || order[i] >= size || seen[order[i]])
                    return false;

                seen[order[i]] = true;
            }

            return true;
        };

        // Legacy Direction follows the released Up/Down choice for every size.
        for (int size = 1; size <= strum::maxChordTones; ++size)
        {
            int order[strum::maxChordTones] {};
            bool alternateDown = false;
            juce::uint32 randomState = 0x12345678u;

            strum::buildOrder (order, size, strum::legacyDirectionPattern, false,
                               alternateDown, randomState);

            for (int i = 0; i < size; ++i)
                require (order[i] == i, "Legacy Up order is wrong");

            strum::buildOrder (order, size, strum::legacyDirectionPattern, true,
                               alternateDown, randomState);

            for (int i = 0; i < size; ++i)
                require (order[i] == size - 1 - i, "Legacy Down order is wrong");
        }

        // Every pattern produces a valid permutation for every chord size.
        for (int pattern = 0; pattern < strum::numPatterns; ++pattern)
        {
            for (int size = 1; size <= strum::maxChordTones; ++size)
            {
                for (int trial = 0; trial < 32; ++trial)
                {
                    int order[strum::maxChordTones] {};
                    bool alternateDown = (trial & 1) != 0;
                    juce::uint32 randomState = 0x9e3779b9u
                        + static_cast<juce::uint32> (trial) * 2654435761u;

                    strum::buildOrder (order, size, pattern, (trial & 2) != 0,
                                       alternateDown, randomState);

                    require (isPermutation (order, size),
                             "Order generator produced an invalid permutation");
                }
            }
        }

        // Alternate starts Up, toggles on each trigger, and keeps per-channel
        // state independent.
        {
            bool channelOneDown = false;
            bool channelTwoDown = false;
            juce::uint32 randomState = 0x12345678u;
            int order[strum::maxChordTones] {};

            strum::buildOrder (order, 4, strum::alternatePattern, false,
                               channelOneDown, randomState);
            require (order[0] == 0 && order[3] == 3,
                     "Alternate did not start Up");
            require (channelOneDown, "Alternate did not toggle after Up");

            strum::buildOrder (order, 4, strum::alternatePattern, false,
                               channelTwoDown, randomState);
            require (order[0] == 0 && order[3] == 3,
                     "Another channel did not start Up");
            require (channelTwoDown, "Another channel did not toggle after Up");

            strum::buildOrder (order, 4, strum::alternatePattern, false,
                               channelOneDown, randomState);
            require (order[0] == 3 && order[3] == 0,
                     "Alternate did not follow Up with Down");
            require (! channelOneDown, "Alternate did not toggle back to Up");

            strum::buildOrder (order, 4, strum::alternatePattern, false,
                               channelTwoDown, randomState);
            require (order[0] == 3 && order[3] == 0,
                     "Independent channel did not reach Down");

            channelOneDown = false;
            strum::buildOrder (order, 4, strum::alternatePattern, false,
                               channelOneDown, randomState);
            require (order[0] == 0 && order[3] == 3,
                     "Alternate did not reset to Up");
        }

        // Outside-In emits low, high, second-low, second-high, ...
        for (int size = 1; size <= strum::maxChordTones; ++size)
        {
            int order[strum::maxChordTones] {};
            bool alternateDown = false;
            juce::uint32 randomState = 0;

            strum::buildOrder (order, size, strum::outsideInPattern, false,
                               alternateDown, randomState);

            int low = 0;
            int high = size - 1;

            for (int i = 0; i < size; ++i)
            {
                const auto expected = (i & 1) == 0 ? low++ : high--;
                require (order[i] == expected, "Outside-In order is wrong");
            }
        }

        // Random is a fixed Fisher-Yates sequence for a known seed.
        {
            juce::uint32 randomState = 0x12345678u;
            bool alternateDown = false;
            int order[strum::maxChordTones] {};

            strum::buildOrder (order, 8, strum::randomPattern, false,
                               alternateDown, randomState);

            const int expectedFirst[strum::maxChordTones] = { 3, 1, 7, 0, 2, 4, 6, 5 };

            for (int i = 0; i < 8; ++i)
                require (order[i] == expectedFirst[i],
                         "Random order changed for the test seed");

            strum::buildOrder (order, 8, strum::randomPattern, false,
                               alternateDown, randomState);

            const int expectedSecond[strum::maxChordTones] = { 6, 7, 5, 4, 0, 3, 2, 1 };

            for (int i = 0; i < 8; ++i)
                require (order[i] == expectedSecond[i],
                         "Random state did not advance deterministically");
        }

        // Resetting the state reproduces the same sequence.
        {
            juce::uint32 firstState = 0x9e3779b9u;
            juce::uint32 secondState = 0x9e3779b9u;
            bool firstDown = false;
            bool secondDown = false;
            int first[strum::maxChordTones] {};
            int second[strum::maxChordTones] {};

            strum::buildOrder (first, 6, strum::randomPattern, false,
                               firstDown, firstState);
            strum::buildOrder (second, 6, strum::randomPattern, false,
                               secondDown, secondState);

            for (int i = 0; i < 6; ++i)
                require (first[i] == second[i],
                         "Random sequence did not repeat after reset");

            require (firstState == secondState,
                     "Random state did not repeat after reset");
        }

        // Deterministic patterns leave the random state untouched.
        {
            juce::uint32 randomState = 0x12345678u;
            const auto before = randomState;
            bool alternateDown = false;
            int order[strum::maxChordTones] {};

            strum::buildOrder (order, 4, strum::legacyDirectionPattern, false,
                               alternateDown, randomState);
            strum::buildOrder (order, 4, strum::alternatePattern, false,
                               alternateDown, randomState);
            strum::buildOrder (order, 4, strum::outsideInPattern, false,
                               alternateDown, randomState);

            require (randomState == before,
                     "A deterministic pattern advanced the random state");
        }

        // Non-legacy patterns ignore the released Up/Down selector.
        {
            bool alternateDown = false;
            juce::uint32 randomState = 0x12345678u;
            int up[8] {};
            int down[8] {};

            strum::buildOrder (up, 6, strum::alternatePattern, false,
                               alternateDown, randomState);
            alternateDown = false;
            strum::buildOrder (down, 6, strum::alternatePattern, true,
                               alternateDown, randomState);

            for (int i = 0; i < 6; ++i)
                require (up[i] == down[i], "Alternate did not override Up/Down");

            juce::uint32 upState = 0x12345678u;
            juce::uint32 downState = 0x12345678u;

            strum::buildOrder (up, 8, strum::randomPattern, false,
                               alternateDown, upState);
            strum::buildOrder (down, 8, strum::randomPattern, true,
                               alternateDown, downState);

            for (int i = 0; i < 8; ++i)
                require (up[i] == down[i], "Random did not override Up/Down");

            strum::buildOrder (up, 7, strum::outsideInPattern, false,
                               alternateDown, randomState);
            strum::buildOrder (down, 7, strum::outsideInPattern, true,
                               alternateDown, randomState);

            for (int i = 0; i < 7; ++i)
                require (up[i] == down[i], "Outside-In did not override Up/Down");
        }

        // Defensive bounds: null pointers and empty chords are harmless, and
        // oversized requests clamp to the fixed eight-tone array.
        {
            bool alternateDown = false;
            juce::uint32 randomState = 0x12345678u;
            int order[16] {};
            order[8] = 77;

            strum::buildOrder (nullptr, 8, strum::randomPattern, false,
                               alternateDown, randomState);
            strum::buildOrder (order, 0, strum::randomPattern, false,
                               alternateDown, randomState);
            require (order[8] == 77, "Empty chord wrote outside its range");

            strum::buildOrder (order, 12, strum::randomPattern, false,
                               alternateDown, randomState);
            require (order[8] == 77, "Oversized chord wrote past eight tones");

            for (int i = 0; i < strum::maxChordTones; ++i)
            {
                require (order[i] >= 0 && order[i] < strum::maxChordTones,
                         "Oversized chord produced an out-of-range index");

                for (int j = 0; j < i; ++j)
                    require (order[i] != order[j],
                             "Oversized chord duplicated an index");
            }
        }
    }

    void testStrumPatternParameters()
    {
        namespace strum = boilerplate::dsp::strum;
        using namespace boilerplate::parameters;

        PluginProcessor processor;
        auto& state = processor.getValueTreeState();
        auto* pattern = dynamic_cast<juce::AudioParameterChoice*> (
            state.getParameter (strumPattern));

        require (pattern != nullptr, "Strum Pattern parameter is missing");
        require (pattern->choices == strum::patternNames(),
                 "Strum Pattern choices do not match the frozen table");
        require (pattern->getIndex() == strum::defaultPatternIndex,
                 "Strum Pattern does not default to Legacy Direction");
        require (pattern->getVersionHint() == 20,
                 "Strum Pattern AU hint is wrong");

        // Session round-trip.
        setParameter (processor, strumPattern, 3.0f);

        juce::MemoryBlock saved;
        processor.getStateInformation (saved);

        PluginProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        require (juce::roundToInt (restored.getValueTreeState()
                                       .getRawParameterValue (strumPattern)->load()) == 3,
                 "Strum Pattern session recall failed");

        // Legacy state without the new control falls back to Legacy Direction.
        auto legacy = juce::ValueTree::readFromData (saved.getData(), saved.getSize());

        for (int i = legacy.getNumChildren(); --i >= 0;)
            if (legacy.getChild (i).getProperty ("id").toString() == strumPattern)
                legacy.removeChild (i, nullptr);

        juce::MemoryBlock legacyState;
        {
            juce::MemoryOutputStream stream (legacyState, false);
            legacy.writeToStream (stream);
        }

        PluginProcessor legacyTarget;
        setParameter (legacyTarget, strumPattern, 2.0f);
        legacyTarget.setStateInformation (legacyState.getData(),
                                          static_cast<int> (legacyState.getSize()));
        require (juce::roundToInt (legacyTarget.getValueTreeState()
                                       .getRawParameterValue (strumPattern)->load()) == 0,
                 "Legacy state did not recall Legacy Direction");

        // A user preset preserves the selected pattern.
        juce::TemporaryFile patternPreset (".wickychord");
        wicky::ui::PresetManager patternBank (processor);
        require (patternBank.saveUserPreset (patternPreset.getFile()),
                 "User pattern preset save failed");

        setParameter (processor, strumPattern, 1.0f);

        require (patternBank.loadUserPreset (patternPreset.getFile()),
                 "User pattern preset recall failed");
        require (juce::roundToInt (processor.getValueTreeState()
                                       .getRawParameterValue (strumPattern)->load()) == 3,
                 "User preset did not recall the strum pattern");
    }

    void testStrumPatternEngine()
    {
        namespace strum = boilerplate::dsp::strum;

        const auto noteOn = [] (int channel, int note)
        {
            return juce::MidiMessage::noteOn (channel, note, juce::uint8 (100));
        };

        const auto noteOff = [] (int channel, int note)
        {
            return juce::MidiMessage::noteOff (channel, note);
        };

        const std::vector<int> up { 60, 64, 67 };
        const std::vector<int> down { 67, 64, 60 };
        const std::vector<int> outsideIn { 60, 67, 64 };

        const auto makeScenario = [&] (int pattern, bool sync, int strumMode = 0,
                                       juce::uint32 seed = 0)
        {
            TempoScenario scenario;
            scenario.pattern = pattern;
            scenario.strumMode = strumMode;
            scenario.sync = sync;
            scenario.strumPercent = 100.0f;
            scenario.division = 3;
            scenario.initialBpm = 120.0;
            scenario.blockSize = 4096;
            scenario.randomSeed = seed;
            scenario.totalSamples = sync ? 24000 : 12000;
            scenario.input =
            {
                { 0, noteOn (1, 60) },
                { sync ? 20000 : 10000, noteOff (1, 60) }
            };
            return scenario;
        };

        // Legacy Direction reproduces the released Up/Down behavior in both
        // timing modes and across host block sizes.
        for (const auto sync : { false, true })
        {
            const auto render = [&] (int blockSize, int strumMode)
            {
                auto scenario = makeScenario (strum::legacyDirectionPattern, sync, strumMode);
                scenario.blockSize = blockSize;
                return renderScenario (scenario);
            };

            const auto referenceUp = render (64, 0);
            require (noteOnNotes (referenceUp) == up, "Legacy Up engine order is wrong");
            require (countNoteOns (referenceUp) == 3, "Legacy Up emitted extra note-ons");
            require (noteOffPositions (referenceUp).size() == 3, "Legacy Up lost note-offs");

            const auto referenceDown = render (64, 1);
            require (noteOnNotes (referenceDown) == down, "Legacy Down engine order is wrong");
            require (countNoteOns (referenceDown) == 3, "Legacy Down emitted extra note-ons");
            require (noteOffPositions (referenceDown).size() == 3, "Legacy Down lost note-offs");

            for (const auto blockSize : { 512, 4096 })
            {
                require (sameTimeline (render (blockSize, 0), referenceUp),
                         "Legacy Up depends on host block size");
                require (sameTimeline (render (blockSize, 1), referenceDown),
                         "Legacy Down depends on host block size");
            }
        }

        // Outside-In is low, high, second-low, second-high, ...
        for (const auto sync : { false, true })
        {
            const auto render = [&] (int blockSize)
            {
                auto scenario = makeScenario (strum::outsideInPattern, sync);
                scenario.blockSize = blockSize;
                return renderScenario (scenario);
            };

            const auto reference = render (64);
            require (noteOnNotes (reference) == outsideIn,
                     "Outside-In engine order is wrong");
            require (countNoteOns (reference) == 3, "Outside-In emitted extra note-ons");
            require (noteOffPositions (reference).size() == 3, "Outside-In lost note-offs");

            for (const auto blockSize : { 512, 4096 })
                require (sameTimeline (render (blockSize), reference),
                         "Outside-In depends on host block size");
        }

        // Alternate starts Up and uses the next direction on the next trigger.
        for (const auto sync : { false, true })
        {
            const auto render = [&] (int blockSize)
            {
                auto scenario = makeScenario (strum::alternatePattern, sync);
                scenario.blockSize = blockSize;
                scenario.totalSamples = sync ? 52000 : 27000;
                scenario.input =
                {
                    { 0, noteOn (1, 60) },
                    { sync ? 20000 : 10000, noteOff (1, 60) },
                    { sync ? 30000 : 15000, noteOn (1, 60) },
                    { sync ? 50000 : 25000, noteOff (1, 60) }
                };
                return renderScenario (scenario);
            };

            const auto reference = render (64);
            const auto notes = noteOnNotes (reference);
            require (notes.size() == 6, "Alternate lost a trigger");
            require (std::equal (up.begin(), up.end(), notes.begin()),
                     "Alternate did not start Up");
            require (std::equal (down.begin(), down.end(), notes.begin() + 3),
                     "Alternate did not follow Up with Down");
            require (countNoteOns (reference) == 6, "Alternate emitted extra note-ons");
            require (noteOffPositions (reference).size() == 6, "Alternate lost note-offs");

            for (const auto blockSize : { 512, 4096 })
                require (sameTimeline (render (blockSize), reference),
                         "Alternate depends on host block size");
        }

        // Alternate state is independent per MIDI channel.
        {
            auto scenario = makeScenario (strum::alternatePattern, true);
            scenario.totalSamples = 72000;
            scenario.input =
            {
                { 0, noteOn (1, 60) }, { 20000, noteOff (1, 60) },
                { 25000, noteOn (2, 60) }, { 45000, noteOff (2, 60) },
                { 50000, noteOn (1, 60) }, { 70000, noteOff (1, 60) }
            };

            const auto events = renderScenario (scenario);
            const auto channelOne = noteOnNotes (events, 0);
            const auto channelTwo = noteOnNotes (events, 1);

            require (channelOne.size() == 6, "Alternate lost a channel-one trigger");
            require (std::equal (up.begin(), up.end(), channelOne.begin()),
                     "Channel one did not start Up");
            require (std::equal (down.begin(), down.end(), channelOne.begin() + 3),
                     "Channel one did not alternate to Down");
            require (channelTwo == up, "Channel two inherited channel one's direction");
        }

        // Random plays a permutation and stays block-size independent with a
        // fixed engine seed.
        for (const auto sync : { false, true })
        {
            const auto render = [&] (int blockSize)
            {
                auto scenario = makeScenario (strum::randomPattern, sync, 0, 0x9e3779b9u);
                scenario.blockSize = blockSize;
                return renderScenario (scenario);
            };

            const auto reference = render (64);
            const auto notes = noteOnNotes (reference);
            require (notes.size() == 3, "Random lost a chord tone");
            require (std::is_permutation (notes.begin(), notes.end(),
                                          up.begin(), up.end()),
                     "Random did not play each chord tone once");
            require (countNoteOns (reference) == 3, "Random emitted extra note-ons");
            require (noteOffPositions (reference).size() == 3, "Random lost note-offs");

            for (const auto blockSize : { 512, 4096 })
                require (sameTimeline (render (blockSize), reference),
                         "Random depends on host block size");
        }

        // Re-triggering a held key starts the next Alternate direction and
        // releases the old chord first.
        {
            auto scenario = makeScenario (strum::alternatePattern, false);
            scenario.totalSamples = 22000;
            scenario.input =
            {
                { 0, noteOn (1, 60) },
                { 10000, noteOn (1, 60) },
                { 20000, noteOff (1, 60) }
            };

            const auto events = renderScenario (scenario);
            const auto notes = noteOnNotes (events);
            require (notes.size() == 6, "Alternate retrigger lost a chord");
            require (std::equal (up.begin(), up.end(), notes.begin()),
                     "Alternate retrigger did not start Up");
            require (std::equal (down.begin(), down.end(), notes.begin() + 3),
                     "Alternate retrigger did not use Down");
            require (countNoteOns (events) == 6, "Alternate retrigger duplicated a tone");
            require (noteOffPositions (events).size() == 6,
                     "Alternate retrigger lost a release");
        }

        // Shared tones still use the released ref-count ownership with a
        // non-legacy pattern.
        {
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 64);
            setChordMatrix (processor, 0, 0, 1, 0);
            setParameter (processor, boilerplate::parameters::strumPattern,
                          static_cast<float> (strum::outsideInPattern));
            setParameter (processor, boilerplate::parameters::strum, 0.0f);

            const auto c = processMessages (processor, { noteOn (1, 60) });
            require (hasNoteOn (c, 0, 60) && hasNoteOn (c, 0, 64) && hasNoteOn (c, 0, 67),
                     "Outside-In chord notes are wrong");

            const auto e = processMessages (processor, { noteOn (1, 64) });
            require (hasNoteOn (e, 0, 68) && hasNoteOn (e, 0, 71)
                         && ! hasNoteOn (e, 0, 64),
                     "Shared note was retriggered with Outside-In");

            const auto releaseC = processMessages (processor, { noteOff (1, 60) });
            require (hasNoteOff (releaseC, 0, 60) && hasNoteOff (releaseC, 0, 67)
                         && ! hasNoteOff (releaseC, 0, 64),
                     "Shared note was cut early with Outside-In");

            const auto releaseE = processMessages (processor, { noteOff (1, 64) });
            require (hasNoteOff (releaseE, 0, 64) && hasNoteOff (releaseE, 0, 68)
                         && hasNoteOff (releaseE, 0, 71),
                     "Shared note release failed with Outside-In");
        }

        // Bypass flushes a generated pattern chord and cancels future tones.
        {
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 64);
            setChordMatrix (processor, 0, 0, 1, 0);
            setParameter (processor, boilerplate::parameters::strumPattern,
                          static_cast<float> (strum::outsideInPattern));
            setParameter (processor, boilerplate::parameters::strum, 100.0f);

            const auto first = processMessages (processor, { noteOn (1, 60) });
            require (hasNoteOn (first, 0, 60) && ! hasNoteOn (first, 0, 64),
                     "Pattern first block emitted a future tone");

            setParameter (processor, boilerplate::parameters::bypass, 1.0f);

            const auto bypassed = processMessages (processor, { noteOn (1, 62) });
            require (hasNoteOff (bypassed, 0, 60),
                     "Pattern bypass did not flush the generated tone");
            require (hasNoteOn (bypassed, 0, 62),
                     "Pattern bypass did not pass the input through");
            require (! hasNoteOn (bypassed, 0, 64) && ! hasNoteOn (bypassed, 0, 67),
                     "Pattern bypass left a future tone");

            for (int block = 0; block < 120; ++block)
                require (processMessages (processor, {}).empty(),
                         "Pattern bypass left a pending tone");

            setParameter (processor, boilerplate::parameters::bypass, 0.0f);
            processMessages (processor, { noteOff (1, 62) });
        }
    }

    void testMidiRoutingSettings()
    {
        using namespace wicky::standalone;

        FakeRoutingStore store;
        MidiRoutingSettings settings;
        settings.inputIdentifiers.add ("in-a");
        settings.inputIdentifiers.add ("in-b");
        settings.outputIdentifier = "out-a";
        settings.outputChannel = 5;

        saveMidiRoutingSettings (store, settings);
        require (store.saveCount == 1, "Routing settings were not saved");

        const auto loaded = loadMidiRoutingSettings (store);
        require (loaded.inputIdentifiers == settings.inputIdentifiers,
                 "Routing input identifiers did not survive the round-trip");
        require (loaded.outputIdentifier == "out-a",
                 "Routing output identifier did not survive the round-trip");
        require (loaded.outputChannel == 5,
                 "Routing channel did not survive the round-trip");

        store.setValue (outputChannelKey, "99");
        require (loadMidiRoutingSettings (store).outputChannel == 16,
                 "Routing channel was not clamped to 16");
        store.setValue (outputChannelKey, "-4");
        require (loadMidiRoutingSettings (store).outputChannel == 0,
                 "Routing channel was not clamped to Preserve");

        store.setValue (inputIdentifiersKey, "in-a\n\nin-b\nin-a");
        const auto cleaned = loadMidiRoutingSettings (store);
        require (cleaned.inputIdentifiers.size() == 2
                     && cleaned.inputIdentifiers[0] == "in-a"
                     && cleaned.inputIdentifiers[1] == "in-b",
                 "Routing identifiers were not de-duplicated");

        // A restart sees the same persisted selection.
        FakeMidiBackend backend;
        MidiRouting routing (backend, store);
        require (routing.getSettings().inputIdentifiers == cleaned.inputIdentifiers,
                 "Routing owner did not load persisted inputs");
        require (routing.getSettings().outputIdentifier == "out-a",
                 "Routing owner did not load the persisted output");
    }

    void testMidiChannelRemapper()
    {
        using namespace wicky::standalone;

        const auto collect = +[] (void* context, const juce::MidiMessage& message)
        {
            static_cast<std::vector<juce::MidiMessage>*> (context)->push_back (message);
        };

        const auto noteOn = [] (int channel, int note, int velocity = 100)
        {
            return juce::MidiMessage::noteOn (channel, note,
                                              static_cast<juce::uint8> (velocity));
        };

        const auto noteOff = [] (int channel, int note)
        {
            return juce::MidiMessage::noteOff (channel, note);
        };

        std::vector<juce::MidiMessage> sent;
        MidiChannelRemapper remapper;
        remapper.setOutputChannel (1);

        remapper.process (noteOn (2, 60), collect, &sent);
        require (sent.size() == 1 && sent[0].isNoteOn()
                     && sent[0].getChannel() == 1 && sent[0].getNoteNumber() == 60,
                 "Fixed channel did not remap a note-on");

        sent.clear();
        remapper.process (noteOn (3, 60), collect, &sent);
        require (sent.empty(), "A shared output note was retriggered");

        remapper.process (noteOff (2, 60), collect, &sent);
        require (sent.empty(), "A shared output note was released too early");

        remapper.process (noteOff (3, 60), collect, &sent);
        require (sent.size() == 1 && sent[0].isNoteOff()
                     && sent[0].getChannel() == 1,
                 "The final shared-note owner did not release the note");

        sent.clear();
        remapper.process (juce::MidiMessage::controllerEvent (2, 1, 64), collect, &sent);
        require (sent.size() == 1 && sent[0].getChannel() == 1
                     && sent[0].isController(),
                 "Fixed channel did not remap a controller message");

        // Preserve leaves channels untouched.
        MidiChannelRemapper preserve;
        sent.clear();
        preserve.process (noteOn (5, 64), collect, &sent);
        require (sent.size() == 1 && sent[0].getChannel() == 5,
                 "Preserve changed the output channel");

        sent.clear();
        preserve.process (juce::MidiMessage::controllerEvent (5, 1, 64), collect, &sent);
        require (sent.size() == 1 && sent[0].getChannel() == 5
                     && sent[0].isController(),
                 "Preserve did not forward a controller message");

        // Retriggering the same source releases and restarts the note.
        sent.clear();
        remapper.process (noteOn (1, 72), collect, &sent);
        remapper.process (noteOn (1, 72), collect, &sent);
        require (sent.size() == 3
                     && sent[0].isNoteOn (true)
                     && sent[1].isNoteOff()
                     && sent[2].isNoteOn (true),
                 "Same-source retrigger did not release first");

        // CC123 from one merged source must not cut another source's note.
        MidiChannelRemapper panicRemapper;
        panicRemapper.setOutputChannel (1);
        sent.clear();
        panicRemapper.process (noteOn (1, 60), collect, &sent);
        panicRemapper.process (noteOn (2, 60), collect, &sent);
        sent.clear();
        panicRemapper.process (juce::MidiMessage::allNotesOff (1), collect, &sent);
        require (sent.empty(),
                 "CC123 from one source cut another source's shared note");

        panicRemapper.process (juce::MidiMessage::allNotesOff (2), collect, &sent);
        require (sent.size() == 2
                     && sent[0].isNoteOff()
                     && sent[1].isAllNotesOff(),
                 "The final owner did not release and forward CC123");

        // Panic releases all tracked notes and sends channel safety messages.
        MidiChannelRemapper panic;
        panic.setOutputChannel (1);
        sent.clear();
        panic.process (noteOn (1, 60), collect, &sent);
        panic.process (noteOn (1, 64), collect, &sent);
        sent.clear();
        panic.sendPanic (collect, &sent);
        require (! panic.hasActiveNotes(), "Panic left tracked notes active");
        require (sent.size() >= 34, "Panic did not send all safety messages");
        require (sent[0].isNoteOff() && sent[1].isNoteOff(),
                 "Panic did not send explicit note-offs");
        require (sent[sent.size() - 2].isAllSoundOff()
                     && sent[sent.size() - 1].isAllNotesOff(),
                 "Panic did not end with channel safety messages");

        // An unmatched release passes through.
        sent.clear();
        MidiChannelRemapper unmatched;
        unmatched.setOutputChannel (1);
        unmatched.process (noteOff (1, 80), collect, &sent);
        require (sent.size() == 1 && sent[0].isNoteOff(),
                 "Unmatched note-off was not forwarded");
    }

    void testMidiRoutingRoute()
    {
        using namespace wicky::standalone;

        const auto noteOn = [] (int channel, int note)
        {
            return juce::MidiMessage::noteOn (channel, note, juce::uint8 (100));
        };

        const MidiDeviceDescription inputA { "in-a", "Input A" };
        const MidiDeviceDescription outputA { "out-a", "Output A" };
        const MidiDeviceDescription outputB { "out-b", "Output B" };

        FakeMidiBackend backend;
        backend.inputs.add (inputA);
        backend.outputs.add (outputA);
        backend.outputs.add (outputB);

        FakeRoutingStore store;
        MidiRoutingSettings settings;
        settings.inputIdentifiers.add ("in-a");
        settings.outputIdentifier = "out-a";
        settings.outputChannel = 1;
        saveMidiRoutingSettings (store, settings);

        MidiRouting routing (backend, store);
        routing.refreshInputDevices();
        routing.refreshOutputDevices();

        // Enumeration alone is not a connection.
        require (! routing.isConnected(), "Enumeration was treated as a connection");
        require (! routing.send (noteOn (1, 60)),
                 "Routing sent before an output was opened");
        require (backend.sent.isEmpty(), "Enumeration produced MIDI output");

        // Successful open and send.
        require (routing.openConfiguredRoute(), "Configured route did not open");
        require (routing.isConnected(), "Opened route did not report connected");
        require (backend.openOutputId == "out-a", "Wrong output was opened");
        require (backend.openInputIds.contains ("in-a"), "Selected input was not opened");
        require (routing.send (noteOn (2, 60)), "Connected routing refused a send");
        require (backend.sent.size() == 1 && backend.sent[0].getChannel() == 1,
                 "Routing did not apply the output channel");

        // Input callback uses the routing boundary.
        juce::Array<juce::MidiMessage> received;
        routing.setInputMessageCallback ([&] (const juce::MidiMessage& message)
        {
            received.add (message);
        });
        backend.injectInput (noteOn (3, 72));
        require (received.size() == 1 && received[0].getNoteNumber() == 72,
                 "Input callback was not forwarded");

        // Panic clears shared ownership and sends safety messages.
        routing.send (noteOn (2, 64));
        backend.sent.clear();
        routing.sendPanic();
        require (backend.sent.size() >= 34, "Panic did not flush the route");
        require (backend.sent[0].isNoteOff(), "Panic did not release the held note");

        // Missing output never falls back to another endpoint.
        FakeMidiBackend missingBackend;
        missingBackend.inputs.add (inputA);
        missingBackend.outputs.add (outputB);
        MidiRouting missingRouting (missingBackend, store);
        missingRouting.refreshInputDevices();
        missingRouting.refreshOutputDevices();
        require (! missingRouting.openConfiguredRoute(),
                 "Missing output was reported as connected");
        require (missingRouting.getStatus().outputMissing,
                 "Missing output was not reported");
        require (missingBackend.openOutputId.isEmpty(),
                 "Missing output silently opened a substitute");
        require (! missingRouting.send (noteOn (1, 60)),
                 "Missing output accepted a send");
        require (missingBackend.sent.isEmpty(), "Missing output sent a message");

        // Failed output open is reported and not retried elsewhere.
        FakeMidiBackend failingBackend;
        failingBackend.inputs.add (inputA);
        failingBackend.outputs.add (outputA);
        failingBackend.failOutputOpen = true;
        MidiRouting failingRouting (failingBackend, store);
        failingRouting.refreshInputDevices();
        failingRouting.refreshOutputDevices();
        require (! failingRouting.openConfiguredRoute(),
                 "Failed output open was reported as connected");
        require (failingRouting.getStatus().outputOpenFailed,
                 "Failed output open was not reported");
        require (failingBackend.openOutputId.isEmpty(),
                 "Failed output open left a handle open");

        // Missing and failed inputs are reported while the output still works.
        MidiRoutingSettings missingInputSettings;
        missingInputSettings.inputIdentifiers.add ("in-a");
        missingInputSettings.inputIdentifiers.add ("in-missing");
        missingInputSettings.outputIdentifier = "out-a";

        FakeRoutingStore missingInputStore;
        saveMidiRoutingSettings (missingInputStore, missingInputSettings);

        FakeMidiBackend partialBackend;
        partialBackend.inputs.add (inputA);
        partialBackend.outputs.add (outputA);
        MidiRouting partialRouting (partialBackend, missingInputStore);
        partialRouting.refreshInputDevices();
        partialRouting.refreshOutputDevices();
        require (! partialRouting.openConfiguredRoute(),
                 "Route with a missing input was reported as fully connected");
        require (partialRouting.getStatus().outputConnected,
                 "Output did not stay connected with a missing input");
        require (partialRouting.getStatus().missingInputIdentifiers.contains ("in-missing"),
                 "Missing input was not reported");
        require (partialBackend.openInputIds.contains ("in-a"),
                 "Available input was not opened");
        require (partialRouting.send (noteOn (1, 60)),
                 "Missing input blocked the connected output");

        FakeMidiBackend inputFailBackend;
        inputFailBackend.inputs.add (inputA);
        inputFailBackend.outputs.add (outputA);
        inputFailBackend.failInputOpen = true;
        MidiRouting inputFailRouting (inputFailBackend, store);
        inputFailRouting.refreshInputDevices();
        inputFailRouting.refreshOutputDevices();
        require (! inputFailRouting.openConfiguredRoute(),
                 "Failed input open was reported as fully connected");
        require (inputFailRouting.getStatus().failedInputIdentifiers.contains ("in-a"),
                 "Failed input open was not reported");
        require (inputFailRouting.getStatus().outputConnected,
                 "Output did not stay connected after an input failure");

        // An endpoint that disappears after opening is flushed and reported
        // missing rather than being silently replaced.
        FakeMidiBackend disappearingBackend;
        disappearingBackend.inputs.add (inputA);
        disappearingBackend.outputs.add (outputA);
        MidiRouting disappearingRouting (disappearingBackend, store);
        disappearingRouting.refreshInputDevices();
        disappearingRouting.refreshOutputDevices();
        require (disappearingRouting.openConfiguredRoute(),
                 "Disappearing-output route did not open");
        require (disappearingRouting.send (noteOn (1, 60)),
                 "Disappearing-output route refused a send");
        disappearingBackend.sent.clear();
        disappearingBackend.outputs.clear();
        disappearingRouting.refreshOutputDevices();
        require (! disappearingRouting.isConnected(),
                 "Disappeared output still reported connected");
        require (disappearingRouting.getStatus().outputMissing,
                 "Disappeared output was not reported missing");
        require (! disappearingBackend.isOutputOpen(),
                 "Disappeared output handle was left open");
        require (disappearingBackend.sent.size() >= 33,
                 "Disappeared output was not flushed before closing");

        // Replugging the same endpoint reconnects it explicitly. It is never
        // substituted for a different endpoint while missing.
        disappearingBackend.outputs.add (outputA);
        disappearingBackend.sent.clear();
        require (disappearingRouting.openConfiguredRoute(),
                 "Replugged output did not reopen");
        require (disappearingRouting.isConnected(),
                 "Replugged output did not report connected");
        require (disappearingBackend.sent.isEmpty(),
                 "Reconnecting an idle route emitted MIDI");

        // Shutdown closes inputs and flushes held notes before the endpoint
        // handle goes away.
        FakeMidiBackend shutdownBackend;
        shutdownBackend.inputs.add (inputA);
        shutdownBackend.outputs.add (outputA);

        {
            MidiRouting shutdownRouting (shutdownBackend, store);
            shutdownRouting.refreshInputDevices();
            shutdownRouting.refreshOutputDevices();
            require (shutdownRouting.openConfiguredRoute(),
                     "Shutdown-test route did not open");
            require (shutdownRouting.send (noteOn (1, 72)),
                     "Shutdown-test route refused a send");
            shutdownBackend.sent.clear();
        }

        require (! shutdownBackend.isOutputOpen(),
                 "Shutdown left the output open");
        require (shutdownBackend.openInputIds.isEmpty(),
                 "Shutdown left inputs open");
        require (shutdownBackend.sent.size() >= 33,
                 "Shutdown did not flush held notes");

        // A send failure disconnects the route instead of silently continuing.
        backend.sent.clear();
        backend.failSends = true;
        require (! routing.send (noteOn (1, 64)),
                 "Send failure was reported as success");
        require (! routing.isConnected(), "Send failure left the route connected");
        require (! backend.isOutputOpen(), "Send failure left the output open");
    }

    void testMidiRouteSwitcher()
    {
        using namespace wicky::standalone;

        juce::StringArray order;
        bool openResult = true;
        bool held = false;

        MidiRouteSwitcher switcher (MidiRouteSwitcher::Callbacks {
            [&] { order.add ("detach"); },
            [&] { order.add ("flush"); held = false; },
            [&] (const juce::String& identifier)
            {
                order.add (juce::String ("open:") + identifier);
                return openResult;
            },
            [&] { order.add ("attach"); },
            [&] { order.add ("reset"); }
        });

        held = true;
        require (switcher.switchTo ("out-a"), "Safe switch failed to open out-a");
        require (! held, "Safe switch did not flush held notes");
        require (order.joinIntoString ("|") == "detach|flush|open:out-a|attach|reset",
                 "Safe switch order is wrong");

        order.clear();
        openResult = false;
        require (! switcher.switchTo ("out-missing"),
                 "Failed output open was reported as success");
        require (order.joinIntoString ("|") == "detach|flush|open:out-missing|attach|reset",
                 "Failed switch did not run its recovery steps");

        order.clear();
        openResult = true;
        require (switcher.switchTo ("out-b"), "Recovery switch failed");
        require (order.joinIntoString ("|") == "detach|flush|open:out-b|attach|reset",
                 "Repeated switch order is wrong");

        order.clear();
        openResult = false;
        require (! switcher.switchTo ({}), "Closing the route was reported as success");
        require (order.joinIntoString ("|") == "detach|flush|open:|attach|reset",
                 "Closing the route did not detach and reset");

        MidiRouteSwitcher empty { MidiRouteSwitcher::Callbacks {} };
        require (! empty.switchTo ("out-a"), "Empty switcher reported success");
    }

    void testMidiRoutingProcessorProxy()
    {
        using namespace wicky::standalone;

        FakeMidiSourceProcessor source;
        MidiRoutingProcessorProxy proxy (source);
        proxy.setOutputChannel (1);
        proxy.prepareToPlay (48000.0, 512);

        const auto render = [&] (std::vector<FakeMidiSourceProcessor::Pending> events,
                                 int blockSize = 512)
        {
            source.pending = std::move (events);
            juce::AudioBuffer<float> audio (2, blockSize);
            juce::MidiBuffer midi;
            midi.ensureSize (131072);
            proxy.processBlock (audio, midi);
            return captureMidi (midi);
        };

        auto events = render ({
            { juce::MidiMessage::noteOn (2, 60, juce::uint8 (100)), 100 }
        });
        require (events.size() == 1, "Proxy lost a note-on");
        require (events[0].status == 0x90 && events[0].channel == 0
                     && events[0].data1 == 60 && events[0].position == 100,
                 "Proxy did not remap the note-on or preserve its offset");

        const auto released = render ({
            { juce::MidiMessage::noteOff (2, 60), 0 }
        });
        require (hasNoteOff (released, 0, 60), "Proxy lost a held note release");

        proxy.setOutputChannel (3);
        const auto channelThree = render ({
            { juce::MidiMessage::noteOn (2, 64, juce::uint8 (100)), 10 }
        });
        require (channelThree.size() == 1 && channelThree[0].channel == 2
                     && channelThree[0].data1 == 64,
                 "Proxy channel change did not take effect");

        proxy.setOutputChannel (1);
        proxy.prepareToPlay (48000.0, 512);
        render ({ { juce::MidiMessage::noteOn (2, 72, juce::uint8 (100)), 0 } });
        proxy.requestPanic();

        const auto panic = render ({});
        require (panic.size() >= 33, "Proxy panic did not send safety messages");
        require (hasNoteOff (panic, 0, 72), "Proxy panic did not release the held note");

        for (int channel = 0; channel < 16; ++channel)
            require (hasEvent (panic, 0xb0, channel, 120)
                         && hasEvent (panic, 0xb0, channel, 123),
                     "Proxy panic missed a channel");

        proxy.setProcessor (nullptr);
        require (render ({ { juce::MidiMessage::noteOn (1, 60, juce::uint8 (100)), 0 } }).empty(),
                 "Detached proxy emitted MIDI");

        proxy.setProcessor (&source);
        proxy.prepareToPlay (48000.0, 512);

        // The remap buffer is bounded; a block that expands past it must
        // abandon the partial expansion and panic every channel.
        std::vector<FakeMidiSourceProcessor::Pending> manyEvents;
        manyEvents.reserve (15000);

        for (int i = 0; i < 15000; ++i)
            manyEvents.push_back ({
                juce::MidiMessage::controllerEvent (2, 1, i & 0x7f),
                i % 512
            });

        source.pending = std::move (manyEvents);

        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        midi.ensureSize (200000);
        proxy.processBlock (audio, midi);

        const auto overflow = captureMidi (midi);
        require (overflow.size() == 32, "Remap overflow did not panic");
        for (int channel = 0; channel < 16; ++channel)
            require (hasEvent (overflow, 0xb0, channel, 120)
                         && hasEvent (overflow, 0xb0, channel, 123),
                     "Remap overflow missed a channel");
    }

    void testMidiRoutingPanel()
    {
        using namespace wicky::standalone;

        auto model = std::make_unique<FakeMidiRoutingPanelModel>();
        auto* modelPtr = model.get();
        model->inputs.add ({ "in-a", "Input A" });
        model->outputs.add ({ "out-a", "Output A" });
        model->settings.inputIdentifiers.add ("in-a");
        model->enabledInputs.add ("in-a");
        model->settings.outputIdentifier = "out-a";
        model->settings.outputChannel = 0;

        MidiRoutingPanel panel (std::move (model));
        panel.setSize (560, 460);

        juce::Image image (juce::Image::ARGB, 560, 460, true);
        juce::Graphics graphics (image);
        panel.paintEntireComponent (graphics, true);

        auto* output = dynamic_cast<juce::ComboBox*> (findComponentByName (panel, "Output"));
        auto* channel = dynamic_cast<juce::ComboBox*> (findComponentByName (panel, "Channel"));
        auto* refresh = dynamic_cast<juce::Button*> (findComponentByName (panel, "Refresh"));
        auto* panic = dynamic_cast<juce::Button*> (findComponentByName (panel, "Send Note Offs"));
        auto* status = dynamic_cast<juce::Label*> (findComponentByName (panel, "Status"));
        auto* inputs = findComponentByName (panel, "Inputs");

        require (output != nullptr && channel != nullptr && refresh != nullptr
                     && panic != nullptr && status != nullptr && inputs != nullptr,
                 "MIDI routing panel is missing controls");
        require (output->getNumItems() == 2, "Output list was not populated");
        require (channel->getNumItems() == 17, "Channel list was not populated");
        require (status->getText().contains ("connected"),
                 "Status did not show the connected output");

        output->setSelectedId (1, juce::sendNotificationSync);
        require (modelPtr->lastOutput.isEmpty(), "No Output did not clear the output");

        output->setSelectedId (2, juce::sendNotificationSync);
        require (modelPtr->lastOutput == "out-a", "Output selection did not reach the model");

        channel->setSelectedId (3, juce::sendNotificationSync);
        require (modelPtr->lastChannel == 2, "Channel selection did not reach the model");

        refresh->onClick();
        require (modelPtr->refreshCount == 1, "Refresh did not reach the model");

        panic->onClick();
        require (modelPtr->panicCount == 1, "Send Note Offs did not reach the model");

        modelPtr->setInputEnabled ("in-a", false);
        require (! modelPtr->isInputEnabled ("in-a"), "Input toggle did not reach the model");
    }

    void testHostTempoScheduling()
    {
        using namespace boilerplate::parameters;

        const auto noteOn = [] (int channel, int note)
        {
            return juce::MidiMessage::noteOn (channel, note, juce::uint8 (100));
        };

        const auto noteOff = [] (int channel, int note)
        {
            return juce::MidiMessage::noteOff (channel, note);
        };

        // --- A synced timeline is independent of host block size, transport
        // position and the free-time percentage.
        const auto renderBasic = [&] (int blockSize, float strumPercent)
        {
            TempoScenario scenario;
            scenario.blockSize = blockSize;
            scenario.totalSamples = 24000;
            scenario.sync = true;
            scenario.division = 3; // Straight sixteenth.
            scenario.initialBpm = 120.0;
            scenario.playing = false; // Manual notes must work while stopped.
            scenario.positionJumpAt = 7000;
            scenario.strumPercent = strumPercent;
            scenario.input = { { 0, noteOn (1, 60) }, { 20000, noteOff (1, 60) } };
            return renderScenario (scenario);
        };

        const auto synced = renderBasic (64, 0.0f);
        const auto syncedOns = noteOnPositions (synced);
        require (syncedOns.size() == 3, "Synced chord did not produce three note-ons");
        require (syncedOns[0] == 0 && syncedOns[1] == 6000 && syncedOns[2] == 12000,
                 "Synced sixteenth steps are wrong");
        require (hasNoteOn (synced, 0, 60) && hasNoteOn (synced, 0, 64)
                     && hasNoteOn (synced, 0, 67),
                 "Synced chord notes are wrong");

        const auto syncedOffs = noteOffPositions (synced);
        require (syncedOffs.size() == 3, "Synced chord did not release three notes");

        for (const auto position : syncedOffs)
            require (position == 20000, "Synced release ignored the host timeline");

        for (const auto blockSize : { 512, 4096 })
            require (sameTimeline (renderBasic (blockSize, 0.0f), synced),
                     "Synced strum depends on host block size");

        // Sync uses the division as the full step and ignores the free-time knob.
        require (sameTimeline (renderBasic (64, 100.0f), synced),
                 "Sync mode still used the free-time percentage");

        // --- Tempo changes: already-scheduled tones keep their absolute due
        // samples; chords triggered later use the new tempo.
        const auto renderTempoChange = [&] (int blockSize)
        {
            TempoScenario scenario;
            scenario.blockSize = blockSize;
            scenario.totalSamples = 70000;
            scenario.sync = true;
            scenario.division = 3;
            scenario.initialBpm = 120.0;
            scenario.tempoChanges = { { 3000, 60.0 } };
            scenario.input =
            {
                { 0, noteOn (1, 60) },
                { 20000, noteOff (1, 60) },
                { 30000, noteOn (1, 62) },
                { 60000, noteOff (1, 62) }
            };
            return renderScenario (scenario);
        };

        const auto tempoChange = renderTempoChange (64);
        const auto tempoOns = noteOnPositions (tempoChange);
        require (tempoOns.size() == 6, "Tempo-change scenario lost a chord");
        require (tempoOns[0] == 0 && tempoOns[1] == 6000 && tempoOns[2] == 12000,
                 "Existing chord followed the new tempo");
        require (tempoOns[3] == 30000 && tempoOns[4] == 42000 && tempoOns[5] == 54000,
                 "New chord did not use the new tempo");
        require (hasNoteOn (tempoChange, 0, 62) && hasNoteOn (tempoChange, 0, 66)
                     && hasNoteOn (tempoChange, 0, 69),
                 "New-tempo chord notes are wrong");

        for (const auto blockSize : { 512, 4096 })
            require (sameTimeline (renderTempoChange (blockSize), tempoChange),
                     "Tempo-change timeline depends on host block size");

        // --- Absent and invalid host tempo use the manual fallback.
        for (const auto mode : { 0, 1, 2, 3, 4, 5 })
        {
            TempoScenario scenario;
            scenario.totalSamples = 24000;
            scenario.division = 3;
            scenario.fallbackBpm = 120;
            scenario.input = { { 0, noteOn (1, 60) }, { 20000, noteOff (1, 60) } };

            switch (mode)
            {
                case 0: scenario.tempoAvailable = false; break;
                case 1: scenario.positionAvailable = false; break;
                case 2: scenario.initialBpm = 0.0; break;
                case 3: scenario.initialBpm = std::nan (""); break;
                case 4: scenario.initialBpm = -120.0; break;
                case 5: scenario.attachPlayHead = false; break;
            }

            const auto ons = noteOnPositions (renderScenario (scenario));
            require (ons.size() == 3 && ons[0] == 0 && ons[1] == 6000 && ons[2] == 12000,
                     "Unavailable or invalid host tempo did not use the fallback");
        }

        // The fallback parameter applies when tempo is unavailable, and a valid
        // host tempo overrides it when present.
        {
            TempoScenario noTempo;
            noTempo.totalSamples = 32000;
            noTempo.division = 3;
            noTempo.tempoAvailable = false;
            noTempo.fallbackBpm = 60;
            noTempo.input = { { 0, noteOn (1, 60) }, { 30000, noteOff (1, 60) } };

            const auto ons = noteOnPositions (renderScenario (noTempo));
            require (ons.size() == 3 && ons[0] == 0 && ons[1] == 12000 && ons[2] == 24000,
                     "Fallback BPM was not used");

            TempoScenario hostTempo = noTempo;
            hostTempo.tempoAvailable = true;
            hostTempo.initialBpm = 120.0;

            const auto hostOns = noteOnPositions (renderScenario (hostTempo));
            require (hostOns.size() == 3 && hostOns[0] == 0 && hostOns[1] == 6000
                         && hostOns[2] == 12000,
                     "Host tempo did not override the fallback");
        }

        // --- Free mode ignores the host tempo and keeps the released mapping.
        {
            TempoScenario free;
            free.totalSamples = 10000;
            free.sync = false;
            free.strumPercent = 100.0f;
            free.initialBpm = 60.0;
            free.input = { { 0, noteOn (1, 60) }, { 9000, noteOff (1, 60) } };

            const auto ons = noteOnPositions (renderScenario (free));
            require (ons.size() == 3 && ons[0] == 0 && ons[1] == 2880 && ons[2] == 5760,
                     "Free mode changed or used the host tempo");
        }

        // --- Rapid release cancels future synced tones.
        {
            TempoScenario rapid;
            rapid.blockSize = 512;
            rapid.totalSamples = 2048;
            rapid.sync = true;
            rapid.division = 3;
            rapid.initialBpm = 120.0;
            rapid.input = { { 0, noteOn (1, 60) }, { 100, noteOff (1, 60) } };

            const auto events = renderScenario (rapid);
            require (hasNoteOn (events, 0, 60) && hasNoteOff (events, 0, 60),
                     "Rapid synced release lost the first tone");
            require (! hasNoteOn (events, 0, 64) && ! hasNoteOn (events, 0, 67),
                     "Rapid synced release played a future tone");
            require (countNoteOns (events) == 1, "Rapid synced release played extra tones");
        }

        // --- Staggered release uses the synced step and stays block-independent.
        const auto renderStagger = [&] (int blockSize)
        {
            TempoScenario scenario;
            scenario.blockSize = blockSize;
            scenario.totalSamples = 40000;
            scenario.sync = true;
            scenario.division = 3;
            scenario.initialBpm = 120.0;
            scenario.noteOffMode = 1;
            scenario.input = { { 0, noteOn (1, 60) }, { 20000, noteOff (1, 60) } };
            return renderScenario (scenario);
        };

        const auto staggered = renderStagger (64);
        require (hasNoteOff (staggered, 0, 60) && hasNoteOff (staggered, 0, 64)
                     && hasNoteOff (staggered, 0, 67),
                 "Synced staggered release lost a note");

        const auto staggerOffs = noteOffPositions (staggered);
        require (staggerOffs.size() == 3 && staggerOffs[0] == 20000
                     && staggerOffs[1] == 26000 && staggerOffs[2] == 32000,
                 "Synced staggered release timing is wrong");

        for (const auto blockSize : { 512, 4096 })
            require (sameTimeline (renderStagger (blockSize), staggered),
                     "Synced staggered release depends on host block size");

        // --- Bypass and power flush generated notes in Sync mode.
        for (const auto* switchID : { power, bypass })
        {
            TestPlayHead playHead;
            PluginProcessor processor;
            processor.setPlayHead (&playHead);
            processor.setRateAndBufferSizeDetails (48000.0, 64);
            processor.prepareToPlay (48000.0, 64);
            setChordMatrix (processor, 0, 0, 1, 0);
            setParameter (processor, strumTimingMode, 1.0f);
            setParameter (processor, strumDivision, 3.0f);
            setParameter (processor, strumFallbackBpm, 120.0f);

            const auto first = processMessages (processor, { noteOn (1, 60) });
            require (hasNoteOn (first, 0, 60) && ! hasNoteOn (first, 0, 64),
                     "Synced first block emitted a future tone");

            setParameter (processor, switchID, switchID == power ? 0.0f : 1.0f);

            const auto switched = processMessages (processor, { noteOn (1, 62) });
            require (hasNoteOff (switched, 0, 60),
                     "Bypass/power did not flush the generated tone");
            require (hasNoteOn (switched, 0, 62),
                     "Bypass/power did not pass the input through");
            require (! hasNoteOn (switched, 0, 64) && ! hasNoteOn (switched, 0, 67),
                     "Bypass/power still generated a synced chord");

            for (int block = 0; block < 120; ++block)
            {
                const auto pending = processMessages (processor, {});
                require (pending.empty(), "Bypass/power left a future synced tone pending");
            }

            setParameter (processor, switchID, switchID == power ? 1.0f : 0.0f);
            processMessages (processor, { noteOff (1, 62) });
        }

        // --- Pending-queue exhaustion: extra tones fall back to the current
        // block instead of being swallowed, and every tone still releases.
        {
            TempoScenario pressure;
            pressure.blockSize = 4096;
            pressure.totalSamples = 720000;
            pressure.complexity = 6; // Major 11th: six tones per chord.
            pressure.sync = true;
            pressure.division = 1;   // Dotted eighth: 2.25 s at 20 BPM.
            pressure.initialBpm = 20.0;
            pressure.fallbackBpm = 20;
            pressure.input.clear();

            for (int channel = 0; channel < 16; ++channel)
                for (const int root : { 0, 18, 36, 54, 72, 90, 108 })
                    pressure.input.push_back ({ 0, noteOn (channel + 1, root) });

            const auto totalChords = 16 * 7;

            for (int channel = 0; channel < 16; ++channel)
                for (const int root : { 0, 18, 36, 54, 72, 90, 108 })
                    pressure.input.push_back ({ 700000, noteOff (channel + 1, root) });

            std::vector<CapturedEvent> firstBlock;
            const auto events = renderScenario (pressure, &firstBlock);

            require (countNoteOns (firstBlock) > totalChords,
                     "Pending-queue pressure did not exercise the immediate fallback");
            require (countNoteOns (events) == totalChords * 6,
                     "Pending-queue exhaustion swallowed a note-on");
            require (static_cast<int> (noteOffPositions (events).size()) == totalChords * 6,
                     "Pending-queue exhaustion swallowed a note-off");
        }
    }

    void testGlobalSettingsPanel()
    {
        using namespace boilerplate::parameters;

        TestPlayHead playHead;
        playHead.tempoBpm = 120.0;

        PluginProcessor processor;
        processor.setPlayHead (&playHead);
        processor.setRateAndBufferSizeDetails (48000.0, 64);
        processor.prepareToPlay (48000.0, 64);

        setParameter (processor, strumTimingMode, 1.0f); // Sync
        setParameter (processor, strumDivision, 4.0f);   // Dotted sixteenth
        setParameter (processor, strumFallbackBpm, 90.0f);
        setParameter (processor, strumPattern, 3.0f);    // Outside-In

        wicky::ui::GlobalSettingsPanel panel (processor);
        panel.setSize (420, 340);

        auto* strum = findComponentByName (panel, "Strum");
        auto* fallback = findComponentByName (panel, "Fallback BPM");
        auto* timing = findComponentByName (panel, "Strum Timing");
        auto* division = findComponentByName (panel, "Division");
        auto* pattern = findComponentByName (panel, "Strum Pattern");
        auto* strumMode = findComponentByName (panel, "Strum Mode");
        auto* status = findComponentByName (panel, "Timing Status");

        require (strum != nullptr && fallback != nullptr && timing != nullptr
                     && division != nullptr && pattern != nullptr
                     && strumMode != nullptr && status != nullptr,
                 "Global Settings timing controls are missing");

        auto* strumSlider = dynamic_cast<juce::Slider*> (strum);
        auto* fallbackSlider = dynamic_cast<juce::Slider*> (fallback);
        auto* timingChoice = dynamic_cast<juce::ComboBox*> (timing);
        auto* divisionChoice = dynamic_cast<juce::ComboBox*> (division);
        auto* patternChoice = dynamic_cast<juce::ComboBox*> (pattern);
        auto* strumModeChoice = dynamic_cast<juce::ComboBox*> (strumMode);
        auto* statusLabel = dynamic_cast<juce::Label*> (status);

        require (strumSlider != nullptr && fallbackSlider != nullptr
                     && timingChoice != nullptr && divisionChoice != nullptr
                     && patternChoice != nullptr && strumModeChoice != nullptr
                     && statusLabel != nullptr,
                 "Timing controls have the wrong component types");

        // Sync mode disables the free-time knob and explains why in its tooltip.
        panel.refreshTimingControls();
        require (! strumSlider->isEnabled(), "Strum knob was not disabled in Sync mode");
        require (strumSlider->getTooltip().containsIgnoreCase ("sync"),
                 "Sync tooltip does not explain the disabled knob");
        require (timingChoice->getSelectedId() == 2, "Timing selector did not show Sync");
        require (divisionChoice->getSelectedId() == 5,
                 "Division selector did not show dotted sixteenth");
        require (patternChoice->getSelectedId() == 4,
                 "Pattern selector did not show Outside-In");
        require (std::abs (fallbackSlider->getValue() - 90.0) < 0.01,
                 "Fallback BPM control did not show the parameter value");

        // A non-legacy pattern disables the Up/Down selector and explains why.
        require (! strumModeChoice->isEnabled(),
                 "Up/Down selector was not disabled for a non-legacy pattern");
        require (strumModeChoice->getTooltip().containsIgnoreCase ("pattern"),
                 "Up/Down tooltip does not explain the pattern override");

        // Before a host block, Sync reports the fallback and the chosen BPM.
        require (statusLabel->getText().contains ("FALLBACK"),
                 "Status did not show the fallback before a host block");
        require (statusLabel->getText().contains ("90"),
                 "Status did not show the fallback BPM");
        require (statusLabel->getText().contains ("NO HOST TEMPO"),
                 "Status did not explain why the fallback applies");

        // A valid host tempo flips the readout to HOST.
        {
            juce::AudioBuffer<float> audio (2, 64);
            juce::MidiBuffer midi;
            processor.processBlock (audio, midi);
        }

        panel.refreshTimingControls();
        require (statusLabel->getText().contains ("HOST"),
                 "Status did not switch to the host tempo");
        require (! statusLabel->getText().contains ("FALLBACK"),
                 "Status still reported the fallback with a host tempo");
        require (statusLabel->getText().contains ("120"),
                 "Status did not show the host BPM");

        // Free mode re-enables the Strum knob and restores its tooltip.
        setParameter (processor, strumTimingMode, 0.0f);
        panel.refreshTimingControls();
        require (strumSlider->isEnabled(), "Strum knob stayed disabled in Free mode");
        require (strumSlider->getTooltip().containsIgnoreCase ("free"),
                 "Free tooltip was not restored");
        require (statusLabel->getText() == "STRUM: FREE", "Free status text is wrong");

        // Losing host tempo while Sync is selected returns to the fallback.
        setParameter (processor, strumTimingMode, 1.0f);
        playHead.tempoAvailable = false;

        {
            juce::AudioBuffer<float> audio (2, 64);
            juce::MidiBuffer midi;
            processor.processBlock (audio, midi);
        }

        panel.refreshTimingControls();
        require (! strumSlider->isEnabled(), "Strum knob was not disabled after tempo loss");
        require (statusLabel->getText().contains ("FALLBACK"),
                 "Status did not return to the fallback after tempo loss");
        require (statusLabel->getText().contains ("90"),
                 "Status did not show the selected fallback BPM");

        // Returning to Legacy Direction re-enables Up/Down.
        setParameter (processor, strumPattern, 0.0f);
        panel.refreshTimingControls();
        require (strumModeChoice->isEnabled(),
                 "Up/Down selector did not re-enable for Legacy Direction");
        require (strumModeChoice->getTooltip().containsIgnoreCase ("low-to-high"),
                 "Legacy Up/Down tooltip was not restored");

        // The minimum editor column still contains every control.
        panel.setSize (319, 258);

        for (const auto* name : { "Strum", "Fallback BPM", "Timing Status",
                                  "Spread Mode", "Note Off", "Strum Mode",
                                  "Strum Timing", "Division", "Strum Pattern",
                                  "Voicing", "Scale Root", "Scale Mode" })
        {
            auto* control = findComponentByName (panel, name);
            require (control != nullptr, "Timing control disappeared at minimum size");
            require (control->getWidth() > 0 && control->getHeight() > 0,
                     "Timing control has no visible bounds at minimum size");

            const auto area = panel.getLocalArea (control, control->getLocalBounds());
            require (panel.getLocalBounds().contains (area),
                     "Timing control was clipped at minimum size");
        }
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

            // Minimum-size pass: every timing control must remain visible and
            // inside the editor surface.
            editor->setSize (760, 441);
            juce::Image minImage (juce::Image::ARGB, editor->getWidth(),
                                  editor->getHeight(), true);
            juce::Graphics minGraphics (minImage);
            editor->paintEntireComponent (minGraphics, true);

            for (const auto* name : { "Strum Timing", "Division", "Strum Pattern",
                                      "Strum Mode", "Fallback BPM", "Timing Status" })
            {
                auto* control = findComponentByName (*editor, name);
                require (control != nullptr, "Timing control is missing from the editor");
                require (control->isVisible() && control->getWidth() > 0
                             && control->getHeight() > 0,
                         "Timing control has no visible bounds at minimum size");

                const auto area = editor->getLocalArea (control, control->getLocalBounds());
                require (editor->getLocalBounds().contains (area),
                         "Timing control was clipped at minimum editor size");
            }

            auto* strum = findComponentByName (*editor, "Strum");
            require (strum != nullptr && strum->isEnabled(),
                     "Strum knob did not start enabled in Free mode");

            auto* strumMode = findComponentByName (*editor, "Strum Mode");
            require (strumMode != nullptr && strumMode->isEnabled(),
                     "Up/Down selector did not start enabled for Legacy Direction");

            auto* status = dynamic_cast<juce::Label*> (
                findComponentByName (*editor, "Timing Status"));
            require (status != nullptr && status->getText() == "STRUM: FREE",
                     "Editor timing status did not start in Free mode");

            auto* panel = dynamic_cast<wicky::ui::GlobalSettingsPanel*> (
                findComponentByName (*editor, "Global Settings"));
            require (panel != nullptr, "Global Settings panel was not found");

            setParameter (processor, boilerplate::parameters::strumTimingMode, 1.0f);
            panel->refreshTimingControls();
            require (! strum->isEnabled(),
                     "Strum knob did not disable after switching to Sync");

            setParameter (processor, boilerplate::parameters::strumTimingMode, 0.0f);
            panel->refreshTimingControls();
            require (strum->isEnabled(),
                     "Strum knob did not re-enable after switching to Free");

            setParameter (processor, boilerplate::parameters::strumPattern, 3.0f);
            panel->refreshTimingControls();
            require (! strumMode->isEnabled(),
                     "Up/Down selector did not disable for a non-legacy pattern");

            setParameter (processor, boilerplate::parameters::strumPattern, 0.0f);
            panel->refreshTimingControls();
            require (strumMode->isEnabled(),
                     "Up/Down selector did not re-enable for Legacy Direction");

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
            testStrumTiming();
            testStrumTimingParameters();
            testStrumOrder();
            testStrumPatternParameters();
            testStrumPatternEngine();
            testMidiRoutingSettings();
            testMidiChannelRemapper();
            testMidiRoutingRoute();
            testMidiRouteSwitcher();
            testMidiRoutingProcessorProxy();
            testMidiRoutingPanel();
            testHostTempoScheduling();
            testGlobalSettingsPanel();
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
