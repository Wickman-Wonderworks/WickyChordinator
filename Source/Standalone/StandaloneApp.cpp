#include <juce_core/system/juce_TargetPlatform.h>
#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

// The standalone helper header defines Component/Point as convenience macros.
// Our standalone-only sources use qualified juce:: names.
#if defined(Component)
 #undef Component
#endif
#if defined(Point)
 #undef Point
#endif

#include "Standalone/MidiRouting.h"
#include "Standalone/MidiRoutingPanel.h"

#include <memory>

namespace
{
    //==========================================================================
    wicky::standalone::MidiDeviceList enumerateInputs()
    {
        wicky::standalone::MidiDeviceList result;

        for (const auto& device : juce::MidiInput::getAvailableDevices())
            if (! wicky::standalone::isWickyVirtualInput (device.name))
                result.add ({ device.identifier, device.name });

        return result;
    }

    wicky::standalone::MidiDeviceList enumerateOutputs()
    {
        wicky::standalone::MidiDeviceList result;

        for (const auto& device : juce::MidiOutput::getAvailableDevices())
            result.add ({ device.identifier, device.name });

        if (wicky::standalone::supportsVirtualMidiOutput())
            result.add ({ wicky::standalone::virtualOutputIdentifier,
                          wicky::standalone::virtualOutputName });

        return result;
    }

    const wicky::standalone::MidiDeviceDescription* findDevice (
        const wicky::standalone::MidiDeviceList& devices,
        const juce::String& identifier)
    {
        for (const auto& device : devices)
            if (device.identifier == identifier)
                return &device;

        return nullptr;
    }

    //==========================================================================
    /**
        Standalone holder that owns the routing proxy and persisted MIDI route.

        The real processor remains the editor/state object. The player renders
        through MidiRoutingProcessorProxy so channel remapping has one boundary
        while AudioProcessorPlayer keeps JUCE's sample-offset scheduling.
    */
    class ChordinatorPluginHolder final : public juce::StandalonePluginHolder
    {
    public:
        explicit ChordinatorPluginHolder (juce::PropertySet* settingsToUse)
            : juce::StandalonePluginHolder (
                  settingsToUse,
                  false,
                  juce::String(),
                  nullptr,
                  juce::Array<juce::StandalonePluginHolder::PluginInOuts>(),
                  false),
              proxy (*processor)
        {
            if (auto* properties = dynamic_cast<juce::PropertiesFile*> (settings.get()))
                routingStore = std::make_unique<wicky::standalone::PropertiesFileRoutingStore> (
                    *properties);

            routeSwitcher = std::make_unique<wicky::standalone::MidiRouteSwitcher> (
                wicky::standalone::MidiRouteSwitcher::Callbacks {
                    [this] { player.setMidiOutput (nullptr); },
                    [this] { panicOutput(); },
                    [this] (const juce::String& identifier)
                    {
                        // Own the endpoint here rather than through
                        // AudioDeviceManager, whose setDefaultMidiOutputDevice
                        // stops and restarts every audio callback.
                        midiOutput.reset();
                        midiOutputIdentifier.clear();

                        if (identifier.isNotEmpty())
                        {
                            midiOutput = wicky::standalone::openStandaloneMidiOutput (identifier);

                            if (midiOutput != nullptr)
                            {
                                midiOutputIdentifier = identifier;
                                // Let AudioProcessorPlayer schedule the block
                                // with its sample offsets instead of bursting
                                // every note immediately.
                                midiOutput->startBackgroundThread();
                            }
                        }

                        return midiOutput != nullptr;
                    },
                    [this]
                    {
                        player.setMidiOutput (midiOutput.get());
                    },
                    [this] { proxy.requestRemapperReset(); }
                });

            useProxyProcessor();
            applyPersistedRouting();
            deviceManager.addChangeListener (&deviceWatcher);
        }

        ~ChordinatorPluginHolder() override
        {
            deviceManager.removeChangeListener (&deviceWatcher);

            // Detach before the proxy, endpoint or real processor is destroyed.
            player.setMidiOutput (nullptr);
            panicOutput();
            player.setProcessor (nullptr);
            proxy.setProcessor (nullptr);
            midiOutput.reset();
        }

        /**
            AudioDeviceManager's own callback restores its stock MIDI output on
            every device start. This listener runs after the device callbacks
            have finished and reattaches the routing-panel endpoint.
        */
        class DeviceWatcher final : public juce::ChangeListener, private juce::Timer
        {
        public:
            explicit DeviceWatcher (ChordinatorPluginHolder& ownerToUse)
                : owner (ownerToUse)
            {
                startTimer (500);
            }

            ~DeviceWatcher() override { stopTimer(); }

            void changeListenerCallback (juce::ChangeBroadcaster*) override
            {
                if (owner.deviceManager.getCurrentAudioDevice() != nullptr)
                    owner.attachMidiOutput();
                else
                    owner.panicOutput();
            }

        private:
            void timerCallback() override { owner.reconcileDevices(); }
            ChordinatorPluginHolder& owner;
        };

        void createPlugin() override
        {
            juce::StandalonePluginHolder::createPlugin();
            proxy.setProcessor (processor.get());
        }

        void deletePlugin() override
        {
            player.setProcessor (nullptr);
            panicOutput();
            proxy.setProcessor (nullptr);
            juce::StandalonePluginHolder::deletePlugin();
        }

        void useProxyProcessor()
        {
            player.setProcessor (nullptr);

            if (processor == nullptr)
            {
                proxy.setProcessor (nullptr);
                return;
            }

            proxy.setProcessor (processor.get());
            attachMidiOutput();
            player.setProcessor (&proxy);
        }

        bool switchOutput (const juce::String& identifier)
        {
            return routeSwitcher != nullptr && routeSwitcher->switchTo (identifier);
        }

        juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
        juce::AudioProcessorPlayer& getPlayer() noexcept { return player; }
        wicky::standalone::MidiRoutingProcessorProxy& getProxy() noexcept { return proxy; }
        juce::MidiOutput* getMidiOutput() const noexcept { return midiOutput.get(); }

        const juce::String& getMidiOutputIdentifier() const noexcept
        {
            return midiOutputIdentifier;
        }

        wicky::standalone::MidiRoutingSettingsStore& getRoutingStore() noexcept
        {
            jassert (routingStore != nullptr);
            return *routingStore;
        }

    private:
        void attachMidiOutput()
        {
            if (midiOutput != nullptr)
            {
                if (! midiOutput->isBackgroundThreadRunning())
                    midiOutput->startBackgroundThread();

                player.setMidiOutput (midiOutput.get());
            }
            else
            {
                player.setMidiOutput (nullptr);
            }
        }

        void panicOutput()
        {
            if (midiOutput == nullptr)
                return;

            // Stop the scheduling thread first so its pending notes cannot
            // arrive after the panic. stopBackgroundThread clears the queue.
            midiOutput->stopBackgroundThread();
            wicky::standalone::sendMidiPanic (*midiOutput);
        }

        void reconcileDevices()
        {
            if (routingStore == nullptr)
                return;
            const auto saved = wicky::standalone::loadMidiRoutingSettings (*routingStore);
            const auto inputs = enumerateInputs();
            juce::StringArray connected;
            bool lostInput = false;
            for (const auto& identifier : saved.inputIdentifiers)
            {
                const auto present = findDevice (inputs, identifier) != nullptr;
                if (present)
                {
                    deviceManager.setMidiInputDeviceEnabled (identifier, true);
                    if (deviceManager.isMidiInputDeviceEnabled (identifier))
                        connected.add (identifier);
                }
                else
                {
                    lostInput = lostInput || activeInputIdentifiers.contains (identifier);
                    deviceManager.setMidiInputDeviceEnabled (identifier, false);
                }
            }
            activeInputIdentifiers = connected;
            if (lostInput)
            {
                if (midiOutput != nullptr)
                    midiOutput->clearAllPendingMessages();
                proxy.requestPanic();
            }
            if (midiOutput != nullptr && findDevice (enumerateOutputs(), saved.outputIdentifier) == nullptr)
                switchOutput ({}); // Keep the missing ID in preferences.
        }

        void applyPersistedRouting()
        {
            if (routingStore == nullptr || processor == nullptr)
                return;

            auto settings = wicky::standalone::loadMidiRoutingSettings (*routingStore);
            const auto availableInputs = enumerateInputs();

            // With no saved input key, start from all currently available
            // endpoints. An intentionally empty saved set stays empty.
            if (! routingStore->containsKey (wicky::standalone::inputIdentifiersKey))
            {
                settings.inputIdentifiers.clear();

                for (const auto& device : availableInputs)
                    settings.inputIdentifiers.add (device.identifier);
            }

            wicky::standalone::saveMidiRoutingSettings (*routingStore, settings);

            for (const auto& device : availableInputs)
                deviceManager.setMidiInputDeviceEnabled (
                    device.identifier,
                    settings.inputIdentifiers.contains (device.identifier));

            activeInputIdentifiers = settings.inputIdentifiers;
            proxy.setOutputChannel (settings.outputChannel);

            if (routeSwitcher != nullptr)
                routeSwitcher->switchTo (settings.outputIdentifier);
        }

        std::unique_ptr<wicky::standalone::PropertiesFileRoutingStore> routingStore;
        std::unique_ptr<wicky::standalone::MidiRouteSwitcher> routeSwitcher;
        std::unique_ptr<juce::MidiOutput> midiOutput;
        juce::String midiOutputIdentifier;
        juce::StringArray activeInputIdentifiers;
        DeviceWatcher deviceWatcher { *this };
        wicky::standalone::MidiRoutingProcessorProxy proxy;
    };

    //==========================================================================
    class LiveMidiRoutingModel final : public wicky::standalone::MidiRoutingPanelModel
    {
    public:
        explicit LiveMidiRoutingModel (ChordinatorPluginHolder& holderToUse)
            : holder (holderToUse)
        {
        }

        wicky::standalone::MidiDeviceList getInputDevices() override
        {
            return enumerateInputs();
        }

        wicky::standalone::MidiDeviceList getOutputDevices() override
        {
            return enumerateOutputs();
        }

        wicky::standalone::MidiRoutingSettings getSettings() const override
        {
            return wicky::standalone::loadMidiRoutingSettings (holder.getRoutingStore());
        }

        bool isInputEnabled (const juce::String& identifier) const override
        {
            return holder.getDeviceManager().isMidiInputDeviceEnabled (identifier);
        }

        void setInputEnabled (const juce::String& identifier, bool enabled) override
        {
            holder.getDeviceManager().setMidiInputDeviceEnabled (identifier, enabled);
            if (! enabled)
                sendNoteOffs();

            auto settings = getSettings();

            if (enabled)
                settings.inputIdentifiers.addIfNotAlreadyThere (identifier);
            else
                settings.inputIdentifiers.removeString (identifier);

            save (settings);
        }

        bool setOutput (const juce::String& identifier) override
        {
            const auto opened = holder.switchOutput (identifier);

            auto settings = getSettings();
            settings.outputIdentifier = identifier;
            save (settings);

            return opened;
        }

        void setOutputChannel (int channel) override
        {
            // Panic the old ownership before the remapper moves to the new
            // channel; otherwise held notes could be left on the output.
            holder.getProxy().requestPanic();
            holder.getProxy().setOutputChannel (channel);

            auto settings = getSettings();
            settings.outputChannel = juce::jlimit (0, 16, channel);
            save (settings);
        }

        void refresh() override
        {
            const auto settings = getSettings();

            if (settings.outputIdentifier.isEmpty())
                return;

            const auto outputs = enumerateOutputs();
            const auto present = findDevice (outputs, settings.outputIdentifier) != nullptr;

            // Refresh is user-initiated, so reopen a present endpoint to replace
            // a stale handle after replugging. A missing endpoint is flushed and
            // closed without clearing the saved selection.
            holder.switchOutput (present ? settings.outputIdentifier : juce::String());
        }

        void sendNoteOffs() override
        {
            // Drop notes already queued for future sample offsets, then let the
            // proxy cancel pending chord tones and panic every channel.
            if (auto* output = holder.getMidiOutput())
                output->clearAllPendingMessages();

            holder.getProxy().requestPanic();
        }

        wicky::standalone::MidiRouting::Status getStatus() const override
        {
            wicky::standalone::MidiRouting::Status status;
            const auto settings = getSettings();
            const auto inputs = enumerateInputs();
            const auto outputs = enumerateOutputs();
            auto& deviceManager = holder.getDeviceManager();

            status.outputIdentifier = settings.outputIdentifier;
            status.outputSelected = settings.outputIdentifier.isNotEmpty();
            status.selectedInputIdentifiers = settings.inputIdentifiers;

            if (status.outputSelected)
            {
                if (const auto* device = findDevice (outputs, settings.outputIdentifier))
                {
                    status.outputName = device->name;
                    status.outputConnected = holder.getMidiOutput() != nullptr
                        && holder.getMidiOutputIdentifier()
                               == settings.outputIdentifier;
                }
                else
                {
                    status.outputMissing = true;
                }
            }

            status.outputOpenFailed = status.outputSelected
                && ! status.outputMissing
                && ! status.outputConnected;

            for (const auto& identifier : settings.inputIdentifiers)
            {
                if (findDevice (inputs, identifier) == nullptr)
                    status.missingInputIdentifiers.add (identifier);
                else if (deviceManager.isMidiInputDeviceEnabled (identifier))
                    status.connectedInputIdentifiers.add (identifier);
                else
                    status.failedInputIdentifiers.add (identifier);
            }

            return status;
        }

    private:
        void save (const wicky::standalone::MidiRoutingSettings& settings) const
        {
            wicky::standalone::saveMidiRoutingSettings (holder.getRoutingStore(), settings);
        }

        ChordinatorPluginHolder& holder;
    };

    //==========================================================================
    /**
        The stock standalone window with the routing panel and a custom options
        menu. Audio settings remain the standard JUCE audio device selector;
        MIDI input/output is owned by the routing panel instead.
    */
    class ChordinatorStandaloneWindow final : public juce::StandaloneFilterWindow
    {
    public:
        ChordinatorStandaloneWindow (const juce::String& title,
                                     juce::Colour backgroundColour,
                                     std::unique_ptr<juce::StandalonePluginHolder> holder)
            : juce::StandaloneFilterWindow (title, backgroundColour, std::move (holder))
        {
            hideStockOptionsButton();

            optionsButton.setName ("Options");
            optionsButton.onClick = [this] { showOptionsMenu(); };
            midiButton.setName ("MIDI Routing");
            midiButton.onClick = [this] { showMidiRoutingPanel(); };

            // ResizableWindow's pointer overload hides Component's reference
            // overload here, so pass the addresses explicitly.
            addAndMakeVisible (&optionsButton);
            addAndMakeVisible (&midiButton);
        }

        ~ChordinatorStandaloneWindow() override
        {
            if (auto* dialog = midiDialog.getComponent())
                delete dialog;
        }

        void resized() override
        {
            juce::StandaloneFilterWindow::resized();

            const auto height = getTitleBarHeight() - 8;
            optionsButton.setBounds (8, 6, 60, height);
            midiButton.setBounds (72, 6, 70, height);
        }

    private:
        void hideStockOptionsButton()
        {
            for (int i = 0; i < getNumChildComponents(); ++i)
                if (auto* button = dynamic_cast<juce::TextButton*> (getChildComponent (i)))
                    if (button->getButtonText() == "Options")
                        button->setVisible (false);
        }

        void showOptionsMenu()
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Audio Settings...");
            menu.addItem (2, "MIDI Routing...");
            menu.addSeparator();
            menu.addItem (3, "Save current state...");
            menu.addItem (4, "Load a saved state...");
            menu.addSeparator();
            menu.addItem (5, "Reset to default state");

            menu.showMenuAsync (
                juce::PopupMenu::Options().withTargetComponent (&optionsButton),
                [safe = juce::Component::SafePointer<ChordinatorStandaloneWindow> (this)]
                (int result)
                {
                    if (safe != nullptr && result != 0)
                        safe->handleMenuResult (result);
                });
        }

        void handleMenuResult (int result)
        {
            auto* holder = getPluginHolder();

            switch (result)
            {
                case 1: showAudioSettings(); break;
                case 2: showMidiRoutingPanel(); break;
                case 3: if (holder != nullptr) holder->askUserToSaveState(); break;
                case 4: if (holder != nullptr) holder->askUserToLoadState(); break;
                case 5: resetToDefaultState(); break;
                default: break;
            }
        }

        void showAudioSettings()
        {
            auto* holder = getPluginHolder();

            if (holder == nullptr)
                return;

            auto content = std::make_unique<juce::AudioDeviceSelectorComponent> (
                getDeviceManager(),
                0, holder->getNumInputChannels(),
                0, holder->getNumOutputChannels(),
                false,
                false,
                true,
                false);
            content->setSize (500, 450);

            juce::DialogWindow::LaunchOptions options;
            options.content.setOwned (content.release());
            options.dialogTitle = "Audio Settings";
            options.dialogBackgroundColour =
                juce::LookAndFeel::getDefaultLookAndFeel().findColour (
                    juce::ResizableWindow::backgroundColourId);
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = false;
            options.launchAsync();
        }

        void showMidiRoutingPanel()
        {
            auto* holder = dynamic_cast<ChordinatorPluginHolder*> (getPluginHolder());

            if (holder == nullptr)
                return;

            if (auto* existing = midiDialog.getComponent())
            {
                existing->toFront (true);
                return;
            }

            auto panel = std::make_unique<wicky::standalone::MidiRoutingPanel> (
                std::make_unique<LiveMidiRoutingModel> (*holder));

            juce::DialogWindow::LaunchOptions options;
            options.content.setOwned (panel.release());
            options.dialogTitle = "MIDI Routing";
            options.dialogBackgroundColour =
                juce::LookAndFeel::getDefaultLookAndFeel().findColour (
                    juce::ResizableWindow::backgroundColourId);
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = false;
            midiDialog = options.launchAsync();
        }

        void resetToDefaultState()
        {
            juce::StandaloneFilterWindow::resetToDefaultState();

            if (auto* holder = dynamic_cast<ChordinatorPluginHolder*> (getPluginHolder()))
                holder->useProxyProcessor();
        }

        juce::TextButton optionsButton { "Options" };
        juce::TextButton midiButton { "MIDI" };
        juce::Component::SafePointer<juce::DialogWindow> midiDialog;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordinatorStandaloneWindow)
    };

    //==========================================================================
    class ChordinatorStandaloneApp final : public juce::JUCEApplication
    {
    public:
        ChordinatorStandaloneApp()
        {
            juce::PropertiesFile::Options options;

            options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
            options.filenameSuffix      = ".settings";
            options.osxLibrarySubFolder = "Application Support";
           #if JUCE_LINUX || JUCE_BSD
            options.folderName          = "~/.config";
           #else
            options.folderName          = "";
           #endif

            appProperties.setStorageParameters (options);
        }

        const juce::String getApplicationName() override
        {
            return juce::CharPointer_UTF8 (JucePlugin_Name);
        }

        const juce::String getApplicationVersion() override
        {
            return JucePlugin_VersionString;
        }

        bool moreThanOneInstanceAllowed() override
        {
            return true;
        }

        void anotherInstanceStarted (const juce::String&) override {}

        void initialise (const juce::String&) override
        {
            if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
            {
                pluginHolder = createPluginHolder();
                return;
            }

            mainWindow = std::make_unique<ChordinatorStandaloneWindow> (
                getApplicationName(),
                juce::LookAndFeel::getDefaultLookAndFeel().findColour (
                    juce::ResizableWindow::backgroundColourId),
                createPluginHolder());

            mainWindow->setVisible (true);
        }

        void shutdown() override
        {
            pluginHolder = nullptr;
            mainWindow = nullptr;
            appProperties.saveIfNeeded();
        }

        void systemRequestedQuit() override
        {
            if (pluginHolder != nullptr)
                pluginHolder->savePluginState();

            if (mainWindow != nullptr && mainWindow->pluginHolder != nullptr)
                mainWindow->pluginHolder->savePluginState();

            if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
            {
                juce::Timer::callAfterDelay (100, []
                {
                    if (auto* app = juce::JUCEApplicationBase::getInstance())
                        app->systemRequestedQuit();
                });
            }
            else
            {
                quit();
            }
        }

    private:
        std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
        {
            auto holder = std::make_unique<ChordinatorPluginHolder> (
                appProperties.getUserSettings());

            // Suppress the "Audio input is muted" banner. Input remains muted;
            // only the warning strip is removed.
            holder->processorHasPotentialFeedbackLoop = false;

            return holder;
        }

        juce::ApplicationProperties appProperties;
        std::unique_ptr<ChordinatorStandaloneWindow> mainWindow;
        std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordinatorStandaloneApp)
    };
}

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wmissing-prototypes")
juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new ChordinatorStandaloneApp();
}
JUCE_END_IGNORE_WARNINGS_GCC_LIKE
