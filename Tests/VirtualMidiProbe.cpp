#include "VirtualMidiProbe.h"
#include "PluginProcessor.h"
#include "Standalone/MidiRouting.h"
#include "State/ParameterIDs.h"

#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void check (bool result, const char* reason)
    {
        if (! result)
            throw std::runtime_error (reason);
    }

    struct Receiver final : juce::MidiInputCallback
    {
        struct Event { int status = 0, channel = 0, note = 0; double time = 0; };
        std::array<Event, 6> events {};
        std::atomic<int> count { 0 };
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
        {
            if (! m.isNoteOnOrOff())
                return;
            const auto index = count.load (std::memory_order_relaxed);
            if (index >= static_cast<int> (events.size()))
                return;
            events[static_cast<size_t> (index)] = {
                m.isNoteOn() ? 0x90 : 0x80, m.getChannel(), m.getNoteNumber(), m.getTimeStamp()
            };
            count.store (index + 1, std::memory_order_release);
        }
    };

    int receive (const juce::String& name, const juce::File& ready, const juce::File& result)
    {
        Receiver receiver;
        std::unique_ptr<juce::MidiInput> input;
        for (int attempt = 0; attempt < 100 && input == nullptr; ++attempt)
        {
            for (const auto& device : juce::MidiInput::getAvailableDevices())
                if (device.name == name)
                    input = juce::MidiInput::openDevice (device.identifier, &receiver);
            if (input == nullptr)
                juce::Thread::sleep (50);
        }
        check (input != nullptr, "Second process could not discover/open the virtual endpoint");
        input->start();
        check (ready.replaceWithText ("ready"), "Could not publish receiver readiness");
        for (int attempt = 0; attempt < 200 && receiver.count.load (std::memory_order_acquire) < 6; ++attempt)
            juce::Thread::sleep (50);
        input->stop();
        check (receiver.count.load (std::memory_order_acquire) == 6, "Receiver did not get all six processed events");
        juce::String data;
        for (const auto& event : receiver.events)
            data += juce::String (event.status) + "," + juce::String (event.channel) + ","
                 + juce::String (event.note) + "," + juce::String (event.time, 6) + "\n";
        check (result.replaceWithText (data), "Could not save external MIDI timeline");
        return 0;
    }
}

// Explicit integration check: creates OS MIDI endpoints, but opens no audio device.
// Not part of CTest because CI machines need not have a working MIDI service.
int runVirtualMidiProbe (int argc, char** argv)
{
    using namespace wicky::standalone;
    if (argc == 5 && juce::String (argv[1]) == "--virtual-midi-receiver")
        return receive (argv[2], juce::File (argv[3]), juce::File (argv[4]));

    check (supportsVirtualMidiOutput(), "App-created virtual output is unavailable on this backend");
    auto output = openStandaloneMidiOutput (virtualOutputIdentifier);
    auto second = openStandaloneMidiOutput (virtualOutputIdentifier);
    check (output != nullptr && second != nullptr, "Native virtual MIDI creation failed");
    const auto name = output->getDeviceInfo().name;
    check (name != second->getDeviceInfo().name, "Two virtual instances have the same name");
    second.reset();

    const auto directory = juce::File::getCurrentWorkingDirectory();
    juce::TemporaryFile ready (directory.getNonexistentChildFile ("virtual-midi-ready", ".txt"));
    juce::TemporaryFile result (directory.getNonexistentChildFile ("virtual-midi-result", ".csv"));
    juce::ChildProcess child;
    const juce::StringArray arguments {
        juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
        "--virtual-midi-receiver", name, ready.getFile().getFullPathName(), result.getFile().getFullPathName()
    };
    check (child.start (arguments), "Could not start external MIDI receiver");
    for (int attempt = 0; attempt < 120 && child.isRunning() && ! ready.getFile().existsAsFile(); ++attempt)
        juce::Thread::sleep (50);
    if (! ready.getFile().existsAsFile())
    {
        child.kill();
        throw std::runtime_error (("Receiver did not become ready: " + child.readAllProcessOutput()).toStdString());
    }

    PluginProcessor processor;
    auto& parameters = processor.getValueTreeState();
    const auto set = [&] (const char* id, float value)
    {
        auto* parameter = parameters.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };
    using namespace boilerplate::parameters;
    set (chordType, 0); set (complexity, 0); set (voicing, 1);
    set (voicingStyle, 0); set (humanize, 0); set (humanizeTiming, 0);
    set (strum, 100); set (noteOffMode, 1);
    MidiRoutingProcessorProxy proxy (processor);
    proxy.setOutputChannel (7);
    proxy.prepareToPlay (48000, 24000);
    juce::AudioBuffer<float> audio (0, 24000);
    juce::MidiBuffer midi;
    midi.ensureSize (65536);
    midi.addEvent (juce::MidiMessage::noteOn (2, 60, juce::uint8 (100)), 0);
    midi.addEvent (juce::MidiMessage::noteOff (2, 60), 12000);
    proxy.processBlock (audio, midi);
    output->startBackgroundThread();
    output->sendBlockOfMessages (midi, juce::Time::getMillisecondCounterHiRes() + 100, 48000);
    const auto completed = child.waitForProcessToFinish (12000);
    output->stopBackgroundThread();
    sendMidiPanic (*output);
    if (! completed)
        child.kill();
    check (completed && child.getExitCode() == 0, "External MIDI receiver failed");
    const auto lines = juce::StringArray::fromLines (result.getFile().loadFileAsString());
    double start = 0;
    const int expectedNotes[] { 60, 64, 67, 60, 64, 67 };
    const double expectedTimes[] { 0, .060, .120, .250, .310, .370 };
    for (int i = 0; i < 6; ++i)
    {
        check (i < lines.size(), "External timeline is incomplete");
        const auto fields = juce::StringArray::fromTokens (lines[i], ",", "");
        check (fields.size() == 4 && fields[0].getIntValue() == (i < 3 ? 0x90 : 0x80)
               && fields[1].getIntValue() == 7 && fields[2].getIntValue() == expectedNotes[i],
               "External process received incorrect chord/channel data");
        if (i == 0) start = fields[3].getDoubleValue();
        check (std::abs (fields[3].getDoubleValue() - start - expectedTimes[i]) < .030,
               "External MIDI timing deviated by more than 30 ms");
    }
    std::cout << "External CoreMIDI/ALSA timeline (status,channel,note,timestamp):\n"
              << result.getFile().loadFileAsString() << "Passed\n";
    return 0;
}
