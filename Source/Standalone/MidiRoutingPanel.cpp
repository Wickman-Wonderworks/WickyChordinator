#include "MidiRoutingPanel.h"

namespace wicky::standalone
{
    namespace
    {
        const juce::Colour background { 0xff121214 };
        const juce::Colour surface { 0xff1a1a21 };
        const juce::Colour outline { 0xff0055ff };
        const juce::Colour textPrimary { 0xfff2f2f7 };
        const juce::Colour textMuted { 0xff8a8a99 };
        const juce::Colour accent { 0xffff1493 };
        const juce::Colour highlight { 0xffeeee00 };
        const juce::Colour mint { 0xff39ff14 };
    }

    //==========================================================================
    MidiRoutingPanel::InputListModel::InputListModel (MidiRoutingPanel& ownerToUse)
        : owner (ownerToUse)
    {
    }

    int MidiRoutingPanel::InputListModel::getNumRows()
    {
        return owner.inputs.size();
    }

    void MidiRoutingPanel::InputListModel::paintListBoxItem (
        int row, juce::Graphics& g, int width, int height, bool)
    {
        if (! juce::isPositiveAndBelow (row, owner.inputs.size()))
            return;

        const auto& device = owner.inputs.getReference (row);
        const auto enabled = owner.model != nullptr
            && owner.model->isInputEnabled (device.identifier);

        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (3, 2);
        auto box = area.removeFromLeft (16).reduced (1);

        g.setColour (enabled ? mint : textMuted);
        g.drawRect (box, 1);

        if (enabled)
            g.fillRect (box.reduced (3));

        g.setColour (enabled ? textPrimary : textMuted);
        g.drawText (device.name, area.reduced (6, 0),
                    juce::Justification::centredLeft, true);
    }

    void MidiRoutingPanel::InputListModel::listBoxItemClicked (
        int row, const juce::MouseEvent&)
    {
        if (! juce::isPositiveAndBelow (row, owner.inputs.size())
            || owner.model == nullptr)
            return;

        const auto identifier = owner.inputs.getReference (row).identifier;
        owner.model->setInputEnabled (identifier,
                                      ! owner.model->isInputEnabled (identifier));
        owner.inputList.updateContent();
        owner.updateStatus();
    }

    //==========================================================================
    MidiRoutingPanel::MidiRoutingPanel (
        std::unique_ptr<MidiRoutingPanelModel> modelToUse)
        : model (std::move (modelToUse)),
          inputListModel (*this),
          inputList ("MIDI input devices", &inputListModel)
    {
        setOpaque (true);
        setSize (560, 460);

        titleLabel.setText ("MIDI ROUTING", juce::dontSendNotification);
        titleLabel.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        titleLabel.setColour (juce::Label::textColourId, highlight);

        outputLabel.setText ("OUTPUT", juce::dontSendNotification);
        outputLabel.setColour (juce::Label::textColourId, textMuted);
        outputLabel.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));

        channelLabel.setText ("CHANNEL", juce::dontSendNotification);
        channelLabel.setColour (juce::Label::textColourId, textMuted);
        channelLabel.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));

        inputsLabel.setText ("INPUTS", juce::dontSendNotification);
        inputsLabel.setColour (juce::Label::textColourId, textMuted);
        inputsLabel.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));

        statusLabel.setColour (juce::Label::textColourId, textMuted);
        statusLabel.setName ("Status");
        statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        statusLabel.setJustificationType (juce::Justification::topLeft);

        for (auto* combo : { &outputSelector, &channelSelector })
        {
            combo->setName (combo == &outputSelector ? "Output" : "Channel");
            combo->setColour (juce::ComboBox::backgroundColourId, surface);
            combo->setColour (juce::ComboBox::textColourId, accent);
            combo->setColour (juce::ComboBox::outlineColourId, outline.withAlpha (0.7f));
            combo->setColour (juce::ComboBox::arrowColourId, highlight);
            combo->setColour (juce::ComboBox::focusedOutlineColourId, mint);
            addAndMakeVisible (*combo);
        }

        inputList.setRowHeight (24);
        inputList.setName ("Inputs");
        inputList.setColour (juce::ListBox::backgroundColourId, background);
        inputList.setColour (juce::ListBox::outlineColourId, outline.withAlpha (0.65f));
        inputList.setOutlineThickness (1);
        addAndMakeVisible (inputList);

        refreshButton.setColour (juce::TextButton::buttonColourId, surface);
        refreshButton.setColour (juce::TextButton::textColourOffId, highlight);
        refreshButton.setName ("Refresh");
        panicButton.setColour (juce::TextButton::buttonColourId, surface);
        panicButton.setColour (juce::TextButton::textColourOffId, mint);
        panicButton.setName ("Send Note Offs");
        addAndMakeVisible (refreshButton);
        addAndMakeVisible (panicButton);

        addAndMakeVisible (titleLabel);
        addAndMakeVisible (outputLabel);
        addAndMakeVisible (channelLabel);
        addAndMakeVisible (inputsLabel);
        addAndMakeVisible (statusLabel);

        outputSelector.onChange = [this]
        {
            if (model == nullptr)
                return;

            const auto itemId = outputSelector.getSelectedId();
            const auto index = itemId - 1;

            if (! juce::isPositiveAndBelow (index, outputIdentifiers.size()))
                return;

            model->setOutput (outputIdentifiers[index]);
            updateStatus();
        };

        channelSelector.onChange = [this]
        {
            if (model == nullptr)
                return;

            const auto itemId = channelSelector.getSelectedId();

            if (itemId > 0)
            {
                model->setOutputChannel (itemId - 1);
                updateStatus();
            }
        };

        refreshButton.onClick = [this]
        {
            if (model != nullptr)
            {
                model->refresh();
                refreshFromModel();
            }
        };

        panicButton.onClick = [this]
        {
            if (model != nullptr)
            {
                model->sendNoteOffs();
                refreshFromModel();
            }
        };

        refreshFromModel();
        startTimer (500);
    }

    MidiRoutingPanel::~MidiRoutingPanel() { stopTimer(); }

    void MidiRoutingPanel::timerCallback()
    {
        updateStatus();
    }

    //==========================================================================
    void MidiRoutingPanel::paint (juce::Graphics& g)
    {
        g.fillAll (background);

        g.setColour (outline.withAlpha (0.85f));
        g.drawRect (getLocalBounds(), 1);
    }

    void MidiRoutingPanel::resized()
    {
        auto area = getLocalBounds().reduced (12);

        titleLabel.setBounds (area.removeFromTop (26));
        area.removeFromTop (6);

        auto outputRow = area.removeFromTop (28);
        outputLabel.setBounds (outputRow.removeFromLeft (78));
        outputSelector.setBounds (outputRow);

        area.removeFromTop (4);

        auto channelRow = area.removeFromTop (28);
        channelLabel.setBounds (channelRow.removeFromLeft (78));
        channelSelector.setBounds (channelRow.removeFromLeft (170));

        area.removeFromTop (8);
        inputsLabel.setBounds (area.removeFromTop (18));
        area.removeFromTop (2);

        auto buttonRow = area.removeFromBottom (30);
        refreshButton.setBounds (buttonRow.removeFromLeft (100));
        buttonRow.removeFromLeft (8);
        panicButton.setBounds (buttonRow.removeFromLeft (150));

        area.removeFromBottom (6);
        statusLabel.setBounds (area.removeFromBottom (56));
        area.removeFromBottom (6);

        inputList.setBounds (area);
    }

    //==========================================================================
    void MidiRoutingPanel::rebuildOutputItems()
    {
        outputSelector.clear (juce::dontSendNotification);
        outputIdentifiers.clear();
        outputIdentifiers.add ({});

        outputSelector.addItem ("No Output", 1);
        outputSelector.setTooltip (supportsVirtualMidiOutput()
            ? "Select Wicky Chordinator Virtual Out to publish a port for other applications."
            : "App-created virtual output is unavailable on this backend. Select an existing external loopback port.");

        if (model == nullptr)
            return;

        const auto settings = model->getSettings();
        auto selectedId = 1;
        auto selectedFound = settings.outputIdentifier.isEmpty();

        for (int i = 0; i < outputs.size(); ++i)
        {
            const auto& device = outputs.getReference (i);
            outputIdentifiers.add (device.identifier);
            outputSelector.addItem (device.name, i + 2);

            if (device.identifier == settings.outputIdentifier)
            {
                selectedId = i + 2;
                selectedFound = true;
            }
        }

        if (! supportsVirtualMidiOutput())
        {
            const auto unavailableId = outputs.size() + 100;
            outputSelector.addItem ("Virtual output unavailable - use an existing loopback", unavailableId);
            outputSelector.setItemEnabled (unavailableId, false);
        }

        if (! selectedFound)
        {
            outputIdentifiers.add (settings.outputIdentifier);
            outputSelector.addItem ("Missing: " + settings.outputIdentifier,
                                    outputIdentifiers.size());
            selectedId = outputIdentifiers.size();
        }

        outputSelector.setSelectedId (selectedId, juce::dontSendNotification);
    }

    void MidiRoutingPanel::refreshFromModel()
    {
        if (model == nullptr)
            return;

        inputs = model->getInputDevices();
        outputs = model->getOutputDevices();

        rebuildOutputItems();

        const auto settings = model->getSettings();
        channelSelector.clear (juce::dontSendNotification);
        channelSelector.addItem ("Preserve", 1);

        for (int channel = 1; channel <= 16; ++channel)
            channelSelector.addItem (juce::String (channel), channel + 1);

        channelSelector.setSelectedId (settings.outputChannel + 1,
                                       juce::dontSendNotification);

        inputList.updateContent();
        updateStatus();
    }

    void MidiRoutingPanel::updateStatus()
    {
        if (model == nullptr)
            return;

        const auto status = model->getStatus();
        const auto settings = model->getSettings();
        juce::String text;

        if (settings.outputIdentifier.isEmpty())
            text = "Output: none selected";
        else if (status.outputConnected)
            text = "Output: " + status.outputName + " (connected)";
        else if (status.outputMissing)
            text = "Output: " + settings.outputIdentifier
                 + " (missing - disconnected)";
        else
            text = "Output: " + status.outputName
                 + " (failed to open - disconnected)";

        auto enabledInputs = 0;

        for (const auto& identifier : settings.inputIdentifiers)
            if (model->isInputEnabled (identifier))
                ++enabledInputs;

        text << "\nInputs: " << enabledInputs << " of "
             << settings.inputIdentifiers.size() << " selected";

        if (! status.missingInputIdentifiers.isEmpty())
            text << "\nMissing: " << status.missingInputIdentifiers.joinIntoString (", ");

        if (! status.failedInputIdentifiers.isEmpty())
            text << "\nFailed: " << status.failedInputIdentifiers.joinIntoString (", ");

        statusLabel.setText (text, juce::dontSendNotification);
    }
}
