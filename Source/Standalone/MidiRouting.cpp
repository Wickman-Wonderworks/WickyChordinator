#include "MidiRouting.h"

#include <cstring>

namespace wicky::standalone
{
    namespace
    {
        const MidiDeviceDescription* findDevice (const MidiDeviceList& devices,
                                                 const juce::String& identifier)
        {
            for (const auto& device : devices)
                if (device.identifier == identifier)
                    return &device;

            return nullptr;
        }

        struct SendContext
        {
            MidiRoutingBackend* backend = nullptr;
            bool allSent = true;
        };

        void sendToBackend (void* context, const juce::MidiMessage& message)
        {
            auto& sendContext = *static_cast<SendContext*> (context);

            if (sendContext.backend == nullptr
                || ! sendContext.backend->send (message))
                sendContext.allSent = false;
        }
    }

    //==========================================================================
    bool MidiRoutingSettings::isValid() const noexcept
    {
        return outputChannel >= 0 && outputChannel <= 16;
    }

    //==========================================================================
    PropertiesFileRoutingStore::PropertiesFileRoutingStore (
        juce::PropertiesFile& fileToUse) noexcept
        : file (fileToUse)
    {
    }

    juce::String PropertiesFileRoutingStore::getValue (const juce::String& key) const
    {
        return file.getValue (key);
    }

    bool PropertiesFileRoutingStore::containsKey (const juce::String& key) const
    {
        return file.containsKey (key);
    }

    void PropertiesFileRoutingStore::setValue (const juce::String& key,
                                               const juce::String& value)
    {
        file.setValue (key, value);
    }

    void PropertiesFileRoutingStore::saveIfNeeded()
    {
        file.saveIfNeeded();
    }

    //==========================================================================
    MidiRoutingSettings loadMidiRoutingSettings (const MidiRoutingSettingsStore& store)
    {
        MidiRoutingSettings settings;

        settings.inputIdentifiers = juce::StringArray::fromLines (
            store.getValue (inputIdentifiersKey));
        settings.inputIdentifiers.removeEmptyStrings();
        settings.inputIdentifiers.removeDuplicates (false);

        settings.outputIdentifier = store.getValue (outputIdentifierKey).trim();
        settings.outputChannel = juce::jlimit (
            0, 16, store.getValue (outputChannelKey).getIntValue());

        return settings;
    }

    void saveMidiRoutingSettings (MidiRoutingSettingsStore& store,
                                  const MidiRoutingSettings& settings)
    {
        store.setValue (inputIdentifiersKey,
                        settings.inputIdentifiers.joinIntoString ("\n"));
        store.setValue (outputIdentifierKey, settings.outputIdentifier);
        store.setValue (outputChannelKey,
                        juce::String (juce::jlimit (0, 16, settings.outputChannel)));
        store.saveIfNeeded();
    }

    //==========================================================================
    JuceMidiRoutingBackend::~JuceMidiRoutingBackend()
    {
        closeInputs();
        closeOutput();
    }

    MidiDeviceList JuceMidiRoutingBackend::getInputDevices()
    {
        MidiDeviceList result;

        for (const auto& device : juce::MidiInput::getAvailableDevices())
            result.add ({ device.identifier, device.name });

        return result;
    }

    MidiDeviceList JuceMidiRoutingBackend::getOutputDevices()
    {
        MidiDeviceList result;

        for (const auto& device : juce::MidiOutput::getAvailableDevices())
            result.add ({ device.identifier, device.name });

        return result;
    }

    bool JuceMidiRoutingBackend::openInput (const juce::String& identifier,
                                            juce::MidiInputCallback* callback)
    {
        if (identifier.isEmpty())
            return false;

        if (openInputIdentifiers.contains (identifier))
            return true;

        auto input = juce::MidiInput::openDevice (identifier, this);

        if (input == nullptr)
            return false;

        inputCallback = callback;
        input->start();
        inputs.add (input.release());
        openInputIdentifiers.add (identifier);
        return true;
    }

    void JuceMidiRoutingBackend::closeInputs()
    {
        for (auto* input : inputs)
            input->stop();

        inputs.clear();
        openInputIdentifiers.clear();
        inputCallback = nullptr;
    }

    juce::StringArray JuceMidiRoutingBackend::getOpenInputIdentifiers() const
    {
        return openInputIdentifiers;
    }

    bool JuceMidiRoutingBackend::openOutput (const juce::String& identifier)
    {
        closeOutput();

        if (identifier.isEmpty())
            return false;

        auto newOutput = juce::MidiOutput::openDevice (identifier);

        if (newOutput == nullptr)
            return false;

        output = std::move (newOutput);
        outputIdentifier = identifier;
        return true;
    }

    void JuceMidiRoutingBackend::closeOutput()
    {
        output.reset();
        outputIdentifier.clear();
    }

    bool JuceMidiRoutingBackend::send (const juce::MidiMessage& message)
    {
        return output != nullptr && output->sendMessageNow (message);
    }

    bool JuceMidiRoutingBackend::isOutputOpen() const
    {
        return output != nullptr;
    }

    juce::String JuceMidiRoutingBackend::getOpenOutputIdentifier() const
    {
        return outputIdentifier;
    }

    void JuceMidiRoutingBackend::handleIncomingMidiMessage (
        juce::MidiInput* source, const juce::MidiMessage& message)
    {
        if (inputCallback != nullptr)
            inputCallback->handleIncomingMidiMessage (source, message);
    }

    //==========================================================================
    void MidiChannelRemapper::reset() noexcept
    {
        outputChannel = 0;
        std::memset (held, 0, sizeof (held));
        std::memset (refCounts, 0, sizeof (refCounts));
    }

    void MidiChannelRemapper::setOutputChannel (int channel) noexcept
    {
        outputChannel = juce::jlimit (0, 16, channel);
    }

    int MidiChannelRemapper::getOutputChannel() const noexcept
    {
        return outputChannel;
    }

    void MidiChannelRemapper::releaseNote (int inputChannelIndex, int note,
                                           int outputChannelIndex,
                                           MessageOutput output, void* context) noexcept
    {
        if (! held[inputChannelIndex][note])
            return;

        held[inputChannelIndex][note] = false;

        auto& referenceCount = refCounts[outputChannelIndex][note];

        if (referenceCount > 0)
            --referenceCount;

        if (referenceCount == 0 && output != nullptr)
            output (context, juce::MidiMessage::noteOff (outputChannelIndex + 1, note));
    }

    void MidiChannelRemapper::releaseChannel (int inputChannelIndex,
                                              int outputChannelIndex,
                                              MessageOutput output, void* context) noexcept
    {
        for (int note = 0; note < 128; ++note)
            releaseNote (inputChannelIndex, note, outputChannelIndex, output, context);
    }

    bool MidiChannelRemapper::outputChannelHasNotes (int outputChannelIndex) const noexcept
    {
        for (int note = 0; note < 128; ++note)
            if (refCounts[outputChannelIndex][note] > 0)
                return true;

        return false;
    }

    bool MidiChannelRemapper::hasActiveNotes() const noexcept
    {
        for (int channel = 0; channel < 16; ++channel)
            if (outputChannelHasNotes (channel))
                return true;

        return false;
    }

    void MidiChannelRemapper::releaseAll (MessageOutput output, void* context) noexcept
    {
        for (int channel = 0; channel < 16; ++channel)
        {
            for (int note = 0; note < 128; ++note)
            {
                if (refCounts[channel][note] == 0)
                    continue;

                if (output != nullptr)
                    output (context, juce::MidiMessage::noteOff (channel + 1, note));

                refCounts[channel][note] = 0;
            }
        }

        std::memset (held, 0, sizeof (held));
    }

    void MidiChannelRemapper::sendPanic (MessageOutput output, void* context) noexcept
    {
        releaseAll (output, context);

        if (output == nullptr)
            return;

        for (int channel = 1; channel <= 16; ++channel)
        {
            output (context, juce::MidiMessage::allSoundOff (channel));
            output (context, juce::MidiMessage::allNotesOff (channel));
        }
    }

    void MidiChannelRemapper::process (const juce::MidiMessage& message,
                                       MessageOutput output, void* context) noexcept
    {
        const auto* raw = message.getRawData();
        const auto size = message.getRawDataSize();

        if (raw == nullptr || size <= 0)
            return;

        const auto statusByte = raw[0];

        if (statusByte < 0x80)
            return;

        // System messages have no channel and pass through unchanged.
        if (statusByte >= 0xf0)
        {
            if (output != nullptr)
                output (context, message);

            return;
        }

        const auto inputChannelIndex = statusByte & 0x0f;
        const auto inputChannel = inputChannelIndex + 1;
        const auto outputChannelNumber = outputChannel == 0
                                             ? inputChannel
                                             : outputChannel;
        const auto outputChannelIndex = outputChannelNumber - 1;
        const auto status = statusByte & 0xf0;

        if (status == 0x90 || status == 0x80)
        {
            if (size < 3)
                return;

            const auto note = static_cast<int> (raw[1]);
            const auto velocity = static_cast<int> (raw[2]);
            const auto isNoteOn = status == 0x90 && velocity > 0;

            if (isNoteOn)
            {
                // A repeated note-on from the same source is a retrigger.
                if (held[inputChannelIndex][note])
                    releaseNote (inputChannelIndex, note, outputChannelIndex,
                                 output, context);

                held[inputChannelIndex][note] = true;
                auto& referenceCount = refCounts[outputChannelIndex][note];

                if (referenceCount == 0 && output != nullptr)
                {
                    auto remapped = message;
                    remapped.setChannel (outputChannelNumber);
                    output (context, remapped);
                }

                ++referenceCount;
            }
            else if (held[inputChannelIndex][note])
            {
                releaseNote (inputChannelIndex, note, outputChannelIndex,
                             output, context);
            }
            else if (refCounts[outputChannelIndex][note] == 0 && output != nullptr)
            {
                // An unmatched release passes through without cutting another
                // source's shared note.
                auto remapped = message;
                remapped.setChannel (outputChannelNumber);
                output (context, remapped);
            }

            return;
        }

        if (size < 2)
            return;

        if (status == 0xb0 && size >= 3)
        {
            const auto controller = static_cast<int> (raw[1]);

            if (controller == 120 || controller == 123)
            {
                // Release only this source's ownership. A channel-wide CC would
                // cut notes owned by another input channel when channels merge.
                releaseChannel (inputChannelIndex, outputChannelIndex,
                                output, context);

                if (! outputChannelHasNotes (outputChannelIndex) && output != nullptr)
                {
                    auto remapped = message;
                    remapped.setChannel (outputChannelNumber);
                    output (context, remapped);
                }

                return;
            }
        }

        if (output != nullptr)
        {
            auto remapped = message;
            remapped.setChannel (outputChannelNumber);
            output (context, remapped);
        }
    }

    //==========================================================================
    MidiRouting::MidiRouting (MidiRoutingBackend& backendToUse,
                              MidiRoutingSettingsStore& storeToUse)
        : backend (backendToUse),
          store (storeToUse)
    {
        loadSettings();
    }

    MidiRouting::~MidiRouting()
    {
        closeRoute();
    }

    MidiRoutingSettings MidiRouting::getSettings() const
    {
        return settings;
    }

    void MidiRouting::setSettings (const MidiRoutingSettings& newSettings, bool persist)
    {
        auto sanitized = newSettings;
        sanitized.inputIdentifiers.removeEmptyStrings();
        sanitized.inputIdentifiers.removeDuplicates (false);
        sanitized.outputIdentifier = sanitized.outputIdentifier.trim();
        sanitized.outputChannel = juce::jlimit (0, 16, sanitized.outputChannel);

        const auto routeChanged = sanitized.outputIdentifier != settings.outputIdentifier
            || sanitized.inputIdentifiers != settings.inputIdentifiers;
        const auto channelChanged = sanitized.outputChannel != settings.outputChannel;

        if (routeChanged
            && (backend.isOutputOpen() || ! backend.getOpenInputIdentifiers().isEmpty()))
        {
            closeRoute();
        }
        else if (channelChanged && backend.isOutputOpen())
        {
            sendPanic();
        }

        settings = sanitized;
        remapper.setOutputChannel (settings.outputChannel);

        if (persist)
            saveSettings();

        refreshStatus();
    }

    void MidiRouting::loadSettings()
    {
        if (backend.isOutputOpen() || ! backend.getOpenInputIdentifiers().isEmpty())
            closeRoute();

        settings = loadMidiRoutingSettings (store);
        remapper.setOutputChannel (settings.outputChannel);
        refreshStatus();
    }

    void MidiRouting::saveSettings()
    {
        saveMidiRoutingSettings (store, settings);
    }

    MidiDeviceList MidiRouting::refreshInputDevices()
    {
        knownInputs = backend.getInputDevices();
        refreshStatus();
        return knownInputs;
    }

    MidiDeviceList MidiRouting::refreshOutputDevices()
    {
        knownOutputs = backend.getOutputDevices();

        if (status.outputConnected
            && findDevice (knownOutputs, settings.outputIdentifier) == nullptr)
        {
            // The selected endpoint disappeared. Flush and disconnect instead
            // of silently substituting another endpoint.
            sendPanic();
            backend.closeOutput();
            remapper.reset();
        }

        refreshStatus();
        return knownOutputs;
    }

    MidiRouting::Status MidiRouting::getStatus() const
    {
        return status;
    }

    bool MidiRouting::openConfiguredRoute()
    {
        if (status.outputConnected
            && status.outputIdentifier == settings.outputIdentifier
            && status.selectedInputIdentifiers == settings.inputIdentifiers)
        {
            return status.missingInputIdentifiers.isEmpty()
                && status.failedInputIdentifiers.isEmpty();
        }

        closeRoute();
        refreshInputDevices();
        refreshOutputDevices();

        routeAttempted = true;
        status.outputIdentifier = settings.outputIdentifier;
        status.outputSelected = settings.outputIdentifier.isNotEmpty();
        status.selectedInputIdentifiers = settings.inputIdentifiers;

        if (! status.outputSelected)
            return false;

        const auto* outputDevice = findDevice (knownOutputs, settings.outputIdentifier);

        if (outputDevice == nullptr)
        {
            status.outputMissing = true;
            return false;
        }

        status.outputName = outputDevice->name;

        if (! backend.openOutput (settings.outputIdentifier))
        {
            status.outputOpenFailed = true;
            return false;
        }

        for (const auto& inputIdentifier : settings.inputIdentifiers)
        {
            if (inputIdentifier.isEmpty())
                continue;

            if (findDevice (knownInputs, inputIdentifier) == nullptr)
            {
                status.missingInputIdentifiers.addIfNotAlreadyThere (inputIdentifier);
                continue;
            }

            if (! backend.openInput (inputIdentifier, this))
                status.failedInputIdentifiers.addIfNotAlreadyThere (inputIdentifier);
        }

        remapper.reset();
        remapper.setOutputChannel (settings.outputChannel);
        refreshStatus();

        return status.outputConnected
            && status.missingInputIdentifiers.isEmpty()
            && status.failedInputIdentifiers.isEmpty();
    }

    void MidiRouting::closeRoute()
    {
        if (backend.isOutputOpen())
            sendPanic();

        backend.closeInputs();
        backend.closeOutput();
        remapper.reset();
        status = {};
        routeAttempted = false;
    }

    bool MidiRouting::isConnected() const
    {
        return status.outputConnected && backend.isOutputOpen();
    }

    bool MidiRouting::send (const juce::MidiMessage& message)
    {
        if (! status.outputConnected || ! backend.isOutputOpen())
            return false;

        SendContext sendContext { &backend, true };
        remapper.process (message, &sendToBackend, &sendContext);

        if (! sendContext.allSent)
        {
            status.outputConnected = false;
            status.outputOpenFailed = true;
            backend.closeOutput();
            remapper.reset();
            return false;
        }

        return true;
    }

    void MidiRouting::setInputMessageCallback (InputMessageCallback callback)
    {
        inputCallback = std::move (callback);
    }

    void MidiRouting::sendPanic()
    {
        if (! backend.isOutputOpen())
        {
            remapper.reset();
            return;
        }

        SendContext sendContext { &backend, true };
        remapper.sendPanic (&sendToBackend, &sendContext);

        if (! sendContext.allSent)
        {
            status.outputConnected = false;
            status.outputOpenFailed = true;
            backend.closeOutput();
            remapper.reset();
        }
    }

    void MidiRouting::handleIncomingMidiMessage (juce::MidiInput*,
                                                 const juce::MidiMessage& message)
    {
        if (inputCallback != nullptr)
            inputCallback (message);
    }

    void MidiRouting::refreshStatus()
    {
        status.outputIdentifier = settings.outputIdentifier;
        status.outputSelected = settings.outputIdentifier.isNotEmpty();
        status.outputName.clear();
        status.outputMissing = false;
        status.selectedInputIdentifiers = settings.inputIdentifiers;
        status.connectedInputIdentifiers = backend.getOpenInputIdentifiers();
        status.missingInputIdentifiers.clear();
        status.failedInputIdentifiers.clear();

        // A handle may still exist briefly after a device disappears. Report it
        // as missing rather than simultaneously connected.
        for (int i = status.connectedInputIdentifiers.size(); --i >= 0;)
            if (findDevice (knownInputs, status.connectedInputIdentifiers[i]) == nullptr)
                status.connectedInputIdentifiers.remove (i);

        if (status.outputSelected)
        {
            if (const auto* device = findDevice (knownOutputs, settings.outputIdentifier))
                status.outputName = device->name;
            else
                status.outputMissing = true;
        }

        for (const auto& identifier : settings.inputIdentifiers)
        {
            if (identifier.isEmpty())
                continue;

            const auto known = findDevice (knownInputs, identifier) != nullptr;
            const auto connected = status.connectedInputIdentifiers.contains (identifier);

            if (! known)
                status.missingInputIdentifiers.addIfNotAlreadyThere (identifier);
            else if (routeAttempted && ! connected)
                status.failedInputIdentifiers.addIfNotAlreadyThere (identifier);
        }

        status.outputConnected = backend.isOutputOpen()
            && backend.getOpenOutputIdentifier() == settings.outputIdentifier;

        if (status.outputConnected)
            status.outputMissing = false;

        status.outputOpenFailed = routeAttempted
            && status.outputSelected
            && ! status.outputMissing
            && ! status.outputConnected;
    }

    //==========================================================================
    void sendMidiPanic (juce::MidiOutput& output)
    {
        for (int channel = 1; channel <= 16; ++channel)
        {
            output.sendMessageNow (juce::MidiMessage::allSoundOff (channel));
            output.sendMessageNow (juce::MidiMessage::allNotesOff (channel));
        }
    }

    MidiRouteSwitcher::MidiRouteSwitcher (Callbacks callbacksToUse)
        : callbacks (std::move (callbacksToUse))
    {
    }

    bool MidiRouteSwitcher::switchTo (const juce::String& outputIdentifier)
    {
        // Detach first: the player's setMidiOutput(nullptr) takes the player lock
        // and therefore waits for the audio callback to stop using the old handle.
        if (callbacks.detachPlayerOutput != nullptr)
            callbacks.detachPlayerOutput();

        // Flush while the old endpoint is still open.
        if (callbacks.flushOldOutput != nullptr)
            callbacks.flushOldOutput();

        const auto opened = callbacks.openNewOutput != nullptr
            && callbacks.openNewOutput (outputIdentifier);

        // Attach the new handle, or null when the requested endpoint failed.
        if (callbacks.attachPlayerOutput != nullptr)
            callbacks.attachPlayerOutput();

        if (callbacks.resetRemapper != nullptr)
            callbacks.resetRemapper();

        return opened;
    }

    //==========================================================================
    juce::AudioProcessor::BusesProperties MidiRoutingProcessorProxy::makeBuses (
        const juce::AudioProcessor& processor)
    {
        BusesProperties buses;

        for (int i = 0; i < processor.getBusCount (true); ++i)
            if (const auto* bus = processor.getBus (true, i))
                buses = buses.withInput (bus->getName(),
                                         bus->getDefaultLayout(),
                                         bus->isEnabledByDefault());

        for (int i = 0; i < processor.getBusCount (false); ++i)
            if (const auto* bus = processor.getBus (false, i))
                buses = buses.withOutput (bus->getName(),
                                          bus->getDefaultLayout(),
                                          bus->isEnabledByDefault());

        return buses;
    }
    namespace
    {
        struct ProxySendContext
        {
            juce::MidiBuffer* buffer = nullptr;
            int position = 0;
            bool overflow = false;
        };

        void appendProxyMessage (void* context, const juce::MidiMessage& message)
        {
            auto& proxyContext = *static_cast<ProxySendContext*> (context);
            constexpr int maxBytes = 65536;
            constexpr int overhead = sizeof (juce::int32) + sizeof (juce::uint16);
            const auto size = message.getRawDataSize();

            if (size <= 0
                || size > 65535 - overhead
                || size > maxBytes - overhead - proxyContext.buffer->data.size())
            {
                proxyContext.overflow = true;
                return;
            }

            proxyContext.buffer->addEvent (message, proxyContext.position);
        }
    }

    MidiRoutingProcessorProxy::MidiRoutingProcessorProxy (
        juce::AudioProcessor& processorToUse)
        : juce::AudioProcessor (makeBuses (processorToUse)),
          processor (&processorToUse)
    {
        remappedMidi.ensureSize (65536);
    }

    MidiRoutingProcessorProxy::~MidiRoutingProcessorProxy() = default;

    void MidiRoutingProcessorProxy::setProcessor (
        juce::AudioProcessor* newProcessor) noexcept
    {
        processor = newProcessor;
    }

    juce::AudioProcessor* MidiRoutingProcessorProxy::getProcessor() const noexcept
    {
        return processor;
    }

    void MidiRoutingProcessorProxy::setOutputChannel (int channel) noexcept
    {
        pendingOutputChannel.store (juce::jlimit (0, 16, channel),
                                    std::memory_order_relaxed);
    }

    int MidiRoutingProcessorProxy::getOutputChannel() const noexcept
    {
        return pendingOutputChannel.load (std::memory_order_relaxed);
    }

    void MidiRoutingProcessorProxy::requestPanic() noexcept
    {
        panicRequested.store (true, std::memory_order_relaxed);
    }

    void MidiRoutingProcessorProxy::requestRemapperReset() noexcept
    {
        resetRequested.store (true, std::memory_order_relaxed);
    }

    void MidiRoutingProcessorProxy::prepareToPlay (double sampleRate,
                                                   int samplesPerBlock)
    {
        if (processor != nullptr)
        {
            processor->setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);
            processor->setPlayConfigDetails (getTotalNumInputChannels(),
                                             getTotalNumOutputChannels(),
                                             sampleRate,
                                             samplesPerBlock);
            processor->prepareToPlay (sampleRate, samplesPerBlock);
        }

        remappedMidi.ensureSize (65536);
        remapper.reset();

        const auto channel = pendingOutputChannel.load (std::memory_order_relaxed);
        appliedOutputChannel.store (channel, std::memory_order_relaxed);
        remapper.setOutputChannel (channel);
        panicRequested.store (false, std::memory_order_relaxed);
        resetRequested.store (false, std::memory_order_relaxed);
    }

    void MidiRoutingProcessorProxy::releaseResources()
    {
        if (processor != nullptr)
            processor->releaseResources();

        remapper.reset();
    }

    void MidiRoutingProcessorProxy::processBlock (juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer& midiMessages)
    {
        if (processor == nullptr)
        {
            midiMessages.clear();
            return;
        }

        const auto panic = panicRequested.exchange (false, std::memory_order_relaxed);
        const auto reset = resetRequested.exchange (false, std::memory_order_relaxed);
        const auto channel = pendingOutputChannel.load (std::memory_order_relaxed);

        if (reset || channel != appliedOutputChannel.load (std::memory_order_relaxed))
        {
            appliedOutputChannel.store (channel, std::memory_order_relaxed);
            remapper.reset();
            remapper.setOutputChannel (channel);
        }

        remappedMidi.clear();
        ProxySendContext context { &remappedMidi, 0, false };

        if (panic)
            remapper.sendPanic (&appendProxyMessage, &context);

        processor->processBlock (buffer, midiMessages);

        for (const auto metadata : midiMessages)
        {
            context.position = metadata.samplePosition;
            context.overflow = false;
            remapper.process (metadata.getMessage(), &appendProxyMessage, &context);

            if (context.overflow)
                break;
        }

        midiMessages.clear();

        if (context.overflow)
        {
            // Bounded overload policy: abandon the expanded buffer and silence
            // every channel rather than leave a partial remap behind.
            for (int outputChannel = 1; outputChannel <= 16; ++outputChannel)
            {
                midiMessages.addEvent (juce::MidiMessage::allSoundOff (outputChannel), 0);
                midiMessages.addEvent (juce::MidiMessage::allNotesOff (outputChannel), 0);
            }

            remapper.reset();
        }
        else
        {
            midiMessages.addEvents (remappedMidi, 0, buffer.getNumSamples(), 0);
        }
    }

    void MidiRoutingProcessorProxy::setPlayHead (juce::AudioPlayHead* newPlayHead)
    {
        juce::AudioProcessor::setPlayHead (newPlayHead);

        if (processor != nullptr)
            processor->setPlayHead (newPlayHead);
    }

    const juce::String MidiRoutingProcessorProxy::getName() const
    {
        return processor != nullptr ? processor->getName() : juce::String ("MIDI Routing");
    }

    bool MidiRoutingProcessorProxy::acceptsMidi() const
    {
        return processor != nullptr && processor->acceptsMidi();
    }

    bool MidiRoutingProcessorProxy::producesMidi() const
    {
        return processor != nullptr && processor->producesMidi();
    }

    double MidiRoutingProcessorProxy::getTailLengthSeconds() const
    {
        return processor != nullptr ? processor->getTailLengthSeconds() : 0.0;
    }

    bool MidiRoutingProcessorProxy::isMidiEffect() const
    {
        return processor != nullptr && processor->isMidiEffect();
    }

    bool MidiRoutingProcessorProxy::supportsDoublePrecisionProcessing() const
    {
        return false;
    }

    bool MidiRoutingProcessorProxy::isBusesLayoutSupported (
        const BusesLayout& layouts) const
    {
        return processor != nullptr && processor->checkBusesLayoutSupported (layouts);
    }

    void MidiRoutingProcessorProxy::audioWorkgroupContextChanged (
        const juce::AudioWorkgroup& workgroup)
    {
        if (processor != nullptr)
            processor->audioWorkgroupContextChanged (workgroup);
    }

    juce::AudioProcessorEditor* MidiRoutingProcessorProxy::createEditor()
    {
        return nullptr;
    }

    bool MidiRoutingProcessorProxy::hasEditor() const
    {
        return false;
    }

    int MidiRoutingProcessorProxy::getNumPrograms()
    {
        return processor != nullptr ? processor->getNumPrograms() : 1;
    }

    int MidiRoutingProcessorProxy::getCurrentProgram()
    {
        return processor != nullptr ? processor->getCurrentProgram() : 0;
    }

    void MidiRoutingProcessorProxy::setCurrentProgram (int index)
    {
        if (processor != nullptr)
            processor->setCurrentProgram (index);
    }

    const juce::String MidiRoutingProcessorProxy::getProgramName (int index)
    {
        return processor != nullptr ? processor->getProgramName (index) : juce::String();
    }

    void MidiRoutingProcessorProxy::changeProgramName (int index,
                                                       const juce::String& newName)
    {
        if (processor != nullptr)
            processor->changeProgramName (index, newName);
    }

    void MidiRoutingProcessorProxy::getStateInformation (juce::MemoryBlock& destData)
    {
        if (processor != nullptr)
            processor->getStateInformation (destData);
    }

    void MidiRoutingProcessorProxy::setStateInformation (const void* data,
                                                         int sizeInBytes)
    {
        if (processor != nullptr)
            processor->setStateInformation (data, sizeInBytes);
    }
}
