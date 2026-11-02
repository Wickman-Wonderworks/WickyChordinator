#include <juce_core/system/juce_TargetPlatform.h>
#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
    //==========================================================================
    /**
        The stock standalone window, with the muted-input notification removed.

        The notification is suppressed by clearing the holder's feedback-loop
        flag. Audio input stays muted by default, so there is no feedback risk;
        the flag only controls whether the warning banner is shown.
    */
    class ChordinatorStandaloneWindow final : public juce::StandaloneFilterWindow
    {
    public:
        ChordinatorStandaloneWindow (const juce::String& title,
                                     juce::Colour backgroundColour,
                                     std::unique_ptr<juce::StandalonePluginHolder> holder)
            : juce::StandaloneFilterWindow (title, backgroundColour, std::move (holder))
        {
        }

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
                    if (auto* app = JUCEApplicationBase::getInstance())
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
            const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig;

            auto holder = std::make_unique<juce::StandalonePluginHolder> (
                appProperties.getUserSettings(),
                false,
                juce::String(),
                nullptr,
                channelConfig,
                false);

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
