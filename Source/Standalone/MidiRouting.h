#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

namespace wicky::standalone
{
    /** A stable MIDI endpoint identifier plus its current display name. */
    struct MidiDeviceDescription
    {
        juce::String identifier;
        juce::String name;

        bool operator== (const MidiDeviceDescription& other) const noexcept
        {
            return identifier == other.identifier && name == other.name;
        }
    };

    using MidiDeviceList = juce::Array<MidiDeviceDescription>;

    /**
        Standalone-only MIDI routing preferences.

        Identifiers are device identifiers, never names or list indices. An
        empty output identifier means "no output selected". outputChannel uses
        0 for Preserve and 1..16 for a fixed output channel.
    */
    struct MidiRoutingSettings
    {
        juce::StringArray inputIdentifiers;
        juce::String outputIdentifier;
        int outputChannel = 0;

        bool isValid() const noexcept;
    };

    /** Small storage abstraction so the routing model is testable without files. */
    class MidiRoutingSettingsStore
    {
    public:
        virtual ~MidiRoutingSettingsStore() = default;

        virtual juce::String getValue (const juce::String& key) const = 0;
        virtual bool containsKey (const juce::String& key) const = 0;
        virtual void setValue (const juce::String& key, const juce::String& value) = 0;
        virtual void saveIfNeeded() = 0;
    };

    /** PropertiesFile adapter used by the standalone application. */
    class PropertiesFileRoutingStore final : public MidiRoutingSettingsStore
    {
    public:
        explicit PropertiesFileRoutingStore (juce::PropertiesFile& fileToUse) noexcept;

        juce::String getValue (const juce::String& key) const override;
        bool containsKey (const juce::String& key) const override;
        void setValue (const juce::String& key, const juce::String& value) override;
        void saveIfNeeded() override;

    private:
        juce::PropertiesFile& file;
    };

    inline constexpr const char* inputIdentifiersKey = "standaloneMidiInputs";
    inline constexpr const char* outputIdentifierKey = "standaloneMidiOutput";
    inline constexpr const char* outputChannelKey = "standaloneMidiChannel";

    MidiRoutingSettings loadMidiRoutingSettings (const MidiRoutingSettingsStore& store);
    void saveMidiRoutingSettings (MidiRoutingSettingsStore& store,
                                  const MidiRoutingSettings& settings);

    /**
        Device backend abstraction.

        The real backend owns the JUCE MIDI handles. Tests substitute a fake
        backend and can observe every attempted open and every sent message.
        All methods are message-thread only; none may be called from processBlock.
    */
    class MidiRoutingBackend
    {
    public:
        virtual ~MidiRoutingBackend() = default;

        virtual MidiDeviceList getInputDevices() = 0;
        virtual MidiDeviceList getOutputDevices() = 0;

        virtual bool openInput (const juce::String& identifier,
                                juce::MidiInputCallback* callback) = 0;
        virtual void closeInputs() = 0;
        virtual juce::StringArray getOpenInputIdentifiers() const = 0;

        virtual bool openOutput (const juce::String& identifier) = 0;
        virtual void closeOutput() = 0;

        /** Sends one message to the currently open output. */
        virtual bool send (const juce::MidiMessage& message) = 0;

        virtual bool isOutputOpen() const = 0;
        virtual juce::String getOpenOutputIdentifier() const = 0;
    };

    /** JUCE-backed endpoint owner. Message-thread only. */
    class JuceMidiRoutingBackend final : public MidiRoutingBackend,
                                         private juce::MidiInputCallback
    {
    public:
        JuceMidiRoutingBackend() = default;
        ~JuceMidiRoutingBackend() override;

        MidiDeviceList getInputDevices() override;
        MidiDeviceList getOutputDevices() override;

        bool openInput (const juce::String& identifier,
                        juce::MidiInputCallback* callback) override;
        void closeInputs() override;
        juce::StringArray getOpenInputIdentifiers() const override;

        bool openOutput (const juce::String& identifier) override;
        void closeOutput() override;
        bool send (const juce::MidiMessage& message) override;

        bool isOutputOpen() const override;
        juce::String getOpenOutputIdentifier() const override;

    private:
        void handleIncomingMidiMessage (juce::MidiInput* source,
                                        const juce::MidiMessage& message) override;

        juce::OwnedArray<juce::MidiInput> inputs;
        juce::StringArray openInputIdentifiers;
        std::unique_ptr<juce::MidiOutput> output;
        juce::String outputIdentifier;
        juce::MidiInputCallback* inputCallback = nullptr;
    };

    /**
        Applies the Preserve / fixed-channel policy at the routing boundary.

        When multiple input channels are merged onto one output channel, note
        ownership is reference-counted by output channel and note. A note-off is
        emitted only when the final owner releases it, and channel-wide CC120/123
        messages are forwarded only when no other source still owns a note on the
        target channel. The remapper allocates nothing.
    */
    class MidiChannelRemapper
    {
    public:
        using MessageOutput = void (*) (void* context, const juce::MidiMessage&);

        void reset() noexcept;
        /** Changes the policy. Release tracked notes before calling this. */
        void setOutputChannel (int channel) noexcept;
        int getOutputChannel() const noexcept;

        void process (const juce::MidiMessage& message,
                      MessageOutput output, void* context) noexcept;
        void releaseAll (MessageOutput output, void* context) noexcept;
        void sendPanic (MessageOutput output, void* context) noexcept;

        bool hasActiveNotes() const noexcept;

    private:
        void releaseNote (int inputChannelIndex, int note, int outputChannelIndex,
                          MessageOutput output, void* context) noexcept;
        void releaseChannel (int inputChannelIndex, int outputChannelIndex,
                             MessageOutput output, void* context) noexcept;
        bool outputChannelHasNotes (int outputChannelIndex) const noexcept;

        int outputChannel = 0;
        bool held[16][128] {};
        juce::uint16 refCounts[16][128] {};
    };

    /**
        Single owner for the selected standalone MIDI route.

        The owner loads/saves stable device identifiers, opens only the selected
        endpoints, reports missing or failed endpoints instead of substituting
        another device, and applies channel remapping plus panic at one boundary.
    */
    class MidiRouting final : private juce::MidiInputCallback
    {
    public:
        /** The backend and store must outlive this routing owner. */
        MidiRouting (MidiRoutingBackend& backend, MidiRoutingSettingsStore& store);
        ~MidiRouting() override;

        MidiRoutingSettings getSettings() const;
        void setSettings (const MidiRoutingSettings& newSettings, bool persist);

        void loadSettings();
        void saveSettings();

        MidiDeviceList refreshInputDevices();
        MidiDeviceList refreshOutputDevices();

        struct Status
        {
            bool outputSelected = false;
            bool outputConnected = false;
            bool outputMissing = false;
            bool outputOpenFailed = false;
            juce::String outputIdentifier;
            juce::String outputName;
            juce::StringArray selectedInputIdentifiers;
            juce::StringArray connectedInputIdentifiers;
            juce::StringArray missingInputIdentifiers;
            juce::StringArray failedInputIdentifiers;
        };

        Status getStatus() const;
        bool openConfiguredRoute();
        void closeRoute();
        bool isConnected() const;

        /** Sends through the owned route. Never call this from processBlock. */
        bool send (const juce::MidiMessage& message);

        using InputMessageCallback = std::function<void (const juce::MidiMessage&)>;

        /** Installs an input hook. Set it while inputs are closed; it runs on
            the MIDI input thread and must stay bounded and non-blocking.
        */
        void setInputMessageCallback (InputMessageCallback callback);

        void sendPanic();

    private:
        void handleIncomingMidiMessage (juce::MidiInput* source,
                                        const juce::MidiMessage& message) override;
        void refreshStatus();

        MidiRoutingBackend& backend;
        MidiRoutingSettingsStore& store;
        MidiRoutingSettings settings;
        MidiChannelRemapper remapper;
        MidiDeviceList knownInputs;
        MidiDeviceList knownOutputs;
        Status status;
        InputMessageCallback inputCallback;
        bool routeAttempted = false;
    };

    /** Sends an immediate panic to an already-open endpoint. Message-thread only. */
    void sendMidiPanic (juce::MidiOutput& output);

    /**
        Owns the message-thread order of a safe output switch.

        detachPlayerOutput must block until the audio callback is no longer using
        the old MidiOutput. flushOldOutput then runs while the old handle is still
        open. Only after that is the new endpoint opened and attached. The
        remapper reset happens last. This class is intentionally independent of
        JUCE devices so the order and failure paths can be tested with fakes.
    */
    class MidiRouteSwitcher final
    {
    public:
        struct Callbacks
        {
            std::function<void()> detachPlayerOutput;
            std::function<void()> flushOldOutput;
            std::function<bool (const juce::String&)> openNewOutput;
            std::function<void()> attachPlayerOutput;
            std::function<void()> resetRemapper;
        };

        explicit MidiRouteSwitcher (Callbacks callbacksToUse);

        /** Returns true only when the requested endpoint was actually opened. */
        bool switchTo (const juce::String& outputIdentifier);

    private:
        Callbacks callbacks;
    };

    /**
        Audio-thread proxy for the standalone player.

        The real processor remains the editor/state object. This proxy is what
        AudioProcessorPlayer prepares and renders, so its final MIDI buffer can
        be channel-remapped at one boundary while MidiOutput keeps JUCE's
        sample-offset scheduling. It owns a preallocated remap buffer and an
        allocation-free remapper. Control changes are published with atomics.
    */
    class MidiRoutingProcessorProxy final : public juce::AudioProcessor
    {
    public:
        explicit MidiRoutingProcessorProxy (juce::AudioProcessor& processorToUse);
        ~MidiRoutingProcessorProxy() override;

        /** Message-thread only; detach the player before changing this. */
        void setProcessor (juce::AudioProcessor* newProcessor) noexcept;
        juce::AudioProcessor* getProcessor() const noexcept;

        /** Requests a channel-policy change. Request a panic first if notes may
            still be sounding on the old output channel.
        */
        void setOutputChannel (int channel) noexcept;
        int getOutputChannel() const noexcept;

        /** Requests a panic at the start of the next rendered block. */
        void requestPanic() noexcept;

        /** Requests a remapper ownership reset at the next rendered block. */
        void requestRemapperReset() noexcept;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        void processBlock (juce::AudioBuffer<float>& buffer,
                           juce::MidiBuffer& midiMessages) override;
        using juce::AudioProcessor::processBlock;

        /** The player sets a playhead on the proxy; forward it so the real
            processor keeps the same host/standalone timing view.
        */
        void setPlayHead (juce::AudioPlayHead* newPlayHead) override;

        const juce::String getName() const override;
        bool acceptsMidi() const override;
        bool producesMidi() const override;
        double getTailLengthSeconds() const override;
        bool isMidiEffect() const override;
        bool supportsDoublePrecisionProcessing() const override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void audioWorkgroupContextChanged (const juce::AudioWorkgroup&) override;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override;
        int getNumPrograms() override;
        int getCurrentProgram() override;
        void setCurrentProgram (int index) override;
        const juce::String getProgramName (int index) override;
        void changeProgramName (int index, const juce::String& newName) override;
        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

    private:
        static juce::AudioProcessor::BusesProperties makeBuses (
            const juce::AudioProcessor& processor);

        juce::AudioProcessor* processor = nullptr;
        MidiChannelRemapper remapper;
        juce::MidiBuffer remappedMidi;
        std::atomic<int> pendingOutputChannel { 0 };
        std::atomic<int> appliedOutputChannel { 0 };
        std::atomic<bool> panicRequested { false };
        std::atomic<bool> resetRequested { false };
    };
}
