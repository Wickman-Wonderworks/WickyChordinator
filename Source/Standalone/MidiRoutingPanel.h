#pragma once

#include "Standalone/MidiRouting.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace wicky::standalone
{
    /**
        The panel's testable data/action boundary.

        The live model wraps AudioDeviceManager, the standalone proxy and the
        persistent standalone settings; tests supply a fake model.
    */
    class MidiRoutingPanelModel
    {
    public:
        virtual ~MidiRoutingPanelModel() = default;

        virtual MidiDeviceList getInputDevices() = 0;
        virtual MidiDeviceList getOutputDevices() = 0;
        virtual MidiRoutingSettings getSettings() const = 0;

        virtual bool isInputEnabled (const juce::String& identifier) const = 0;
        virtual void setInputEnabled (const juce::String& identifier, bool enabled) = 0;

        virtual bool setOutput (const juce::String& identifier) = 0;
        virtual void setOutputChannel (int channel) = 0;
        virtual void refresh() = 0;
        virtual void sendNoteOffs() = 0;

        virtual MidiRouting::Status getStatus() const = 0;
    };

    /** Standalone-only MIDI routing panel. Never added to plugin editors. */
    class MidiRoutingPanel final : public juce::Component, private juce::Timer
    {
    public:
        explicit MidiRoutingPanel (std::unique_ptr<MidiRoutingPanelModel> modelToUse);
        ~MidiRoutingPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void refreshFromModel();

    private:
        class InputListModel final : public juce::ListBoxModel
        {
        public:
            explicit InputListModel (MidiRoutingPanel& ownerToUse);

            int getNumRows() override;
            void paintListBoxItem (int row, juce::Graphics&, int width, int height,
                                   bool rowIsSelected) override;
            void listBoxItemClicked (int row, const juce::MouseEvent&) override;

        private:
            MidiRoutingPanel& owner;
        };

        void timerCallback() override;
        void rebuildOutputItems();
        void updateStatus();

        std::unique_ptr<MidiRoutingPanelModel> model;
        MidiDeviceList inputs;
        MidiDeviceList outputs;
        juce::StringArray outputIdentifiers;

        juce::Label titleLabel;
        juce::Label outputLabel;
        juce::Label channelLabel;
        juce::Label inputsLabel;
        juce::Label statusLabel;

        juce::ComboBox outputSelector;
        juce::ComboBox channelSelector;
        InputListModel inputListModel;
        juce::ListBox inputList;
        juce::TextButton refreshButton { "Refresh" };
        juce::TextButton panicButton { "Send Note Offs" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRoutingPanel)
    };
}
