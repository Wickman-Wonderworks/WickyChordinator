#pragma once

#include <atomic>
#include <limits>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "State/ParameterIDs.h"

//==============================================================================
namespace wicky::ui
{
    /** Miami Nights (Radical Neon) palette. */
    namespace miami
    {
        inline const juce::Colour midnightArcade  { 0xff121214 };
        inline const juce::Colour radicalBlue     { 0xff0055ff };
        inline const juce::Colour fluorescentPink { 0xffff1493 };
        inline const juce::Colour highlighterYellow { 0xffeeee00 };
        inline const juce::Colour brightMint      { 0xff39ff14 };

        inline const juce::Colour controlFace     { 0xff232329 };
        inline const juce::Colour controlEdge     { 0xff2f2f38 };
    }

    /**
        Binds one radio button group to one AudioParameterChoice.

        Realtime contract: parameterChanged() can be called on the audio thread
        when a host automates the parameter, so it does exactly one relaxed
        atomic store. All button updates happen from a message-thread timer,
        which means no allocations, locks, string work or Component calls ever
        happen on the audio thread. Clicks are written back to the parameter on
        the message thread with a normal parameter gesture.
    */
    class ButtonGroupAttachment final : private juce::AudioProcessorValueTreeState::Listener,
                                        private juce::Timer
    {
    public:
        ButtonGroupAttachment (juce::AudioProcessorValueTreeState& stateToUse,
                               const juce::String& parameterID,
                               juce::OwnedArray<juce::TextButton>& group)
            : state (stateToUse),
              buttons (group),
              id (parameterID),
              parameter (stateToUse.getParameter (parameterID))
        {
            jassert (parameter != nullptr);
            jassert (! buttons.isEmpty());

            for (int i = 0; i < buttons.size(); ++i)
                if (auto* button = buttons[i])
                    button->onClick = [this, i] { selectButton (i); };

            state.addParameterListener (id, this);

            // Start from the parameter's real value, never from a hard-coded
            // default that could drift away from ParameterLayout.
            applyIndex (indexFromParameter());

            // The listener only leaves a breadcrumb; this timer is the only
            // place where the UI is touched.
            startTimerHz (30);
        }

        ~ButtonGroupAttachment() override
        {
            stopTimer();
            state.removeParameterListener (id, this);
        }

    private:
        //==========================================================================
        void parameterChanged (const juce::String&, float newValue) override
        {
            if (buttons.isEmpty())
                return;

            const auto index = juce::jlimit (0, buttons.size() - 1,
                                             juce::roundToInt (newValue));

            // Audio-thread safe: one relaxed store, nothing else.
            pendingIndex.store (index, std::memory_order_relaxed);
        }

        void timerCallback() override
        {
            const auto index = pendingIndex.exchange (-1, std::memory_order_relaxed);

            if (index >= 0)
                applyIndex (index);
        }

        void selectButton (int index)
        {
            if (parameter == nullptr)
                return;

            const auto alreadySelected = (index == appliedIndex);
            applyIndex (index);

            if (alreadySelected)
                return;

            pendingIndex.store (index, std::memory_order_relaxed);

            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (index)));
            parameter->endChangeGesture();
        }

        void applyIndex (int index)
        {
            if (! juce::isPositiveAndBelow (index, buttons.size()))
                return;

            // The radio group turns the other buttons off for us.
            if (auto* button = buttons[index])
                button->setToggleState (true, juce::dontSendNotification);

            appliedIndex = index;
        }

        int indexFromParameter() const
        {
            if (parameter == nullptr)
                return 0;

            const auto index = juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue()));
            return juce::jlimit (0, buttons.size() - 1, index);
        }

        //==========================================================================
        juce::AudioProcessorValueTreeState& state;
        juce::OwnedArray<juce::TextButton>& buttons;
        juce::String id;
        juce::RangedAudioParameter* parameter = nullptr;

        std::atomic<int> pendingIndex { -1 };
        int appliedIndex = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ButtonGroupAttachment)
    };
}

//==============================================================================
/**
    The chord-specific control surface: Type, Complexity, Voicing and the
    per-chord Pitch Offset. The panel owns no DSP; it only writes APVTS
    parameters through the attachments.
*/
class ChordSettingsPanel final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit ChordSettingsPanel (juce::AudioProcessorValueTreeState& stateToUse)
        : state (stateToUse),
          pitchParameter (stateToUse.getParameter (boilerplate::parameters::pitchOffset)),
          pitchRawParameter (stateToUse.getRawParameterValue (boilerplate::parameters::pitchOffset))
    {
        setName ("Chord Settings");
        setDescription ("Chord type, complexity, voicing and pitch offset");

        jassert (pitchParameter != nullptr);
        jassert (pitchRawParameter != nullptr);

        createButtonGroup (typeButtons, boilerplate::parameters::chordType, 1001);
        createButtonGroup (complexityButtons, boilerplate::parameters::complexity, 1002);
        createButtonGroup (voicingButtons, boilerplate::parameters::voicing, 1003);

        // Declared after the button arrays, so the attachments are destroyed
        // before the buttons they point at.
        typeAttachment = std::make_unique<wicky::ui::ButtonGroupAttachment> (
            state, boilerplate::parameters::chordType, typeButtons);
        complexityAttachment = std::make_unique<wicky::ui::ButtonGroupAttachment> (
            state, boilerplate::parameters::complexity, complexityButtons);
        voicingAttachment = std::make_unique<wicky::ui::ButtonGroupAttachment> (
            state, boilerplate::parameters::voicing, voicingButtons);

        configurePitchControls();

        updatePitchLabel();
        startTimerHz (20);
    }

    ~ChordSettingsPanel() override
    {
        stopTimer();
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
            return;

        g.setColour (wicky::ui::miami::midnightArcade);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (wicky::ui::miami::radicalBlue.withAlpha (0.85f));
        g.drawRoundedRectangle (bounds, 8.0f, 1.5f);

        auto header = bounds.removeFromTop (static_cast<float> (headerHeight));

        juce::Path headerShape;
        headerShape.addRoundedRectangle (header.getX(), header.getY(),
                                         header.getWidth(), header.getHeight(),
                                         8.0f, 8.0f, true, true, false, false);
        g.setColour (wicky::ui::miami::radicalBlue);
        g.fillPath (headerShape);

        // Bright mint functional highlight along the header's lower edge.
        g.setColour (wicky::ui::miami::brightMint.withAlpha (0.85f));
        g.fillRect (header.getX() + 12.0f, header.getBottom() - 2.0f,
                    juce::jmin (64.0f, header.getWidth() - 24.0f), 2.0f);

        g.setColour (juce::Colours::white);
        const auto titleSize = juce::jlimit (13.0f, 20.0f, static_cast<float> (headerHeight) * 0.46f);
        g.setFont (juce::Font (juce::FontOptions (titleSize, juce::Font::bold))
                       .withExtraKerningFactor (0.10f));
        g.drawText ("CHORD SETTINGS", header.reduced (12.0f, 0.0f),
                    juce::Justification::centredLeft, false);

        if (typeColumn.isEmpty() || complexityColumn.isEmpty() || voicingColumn.isEmpty())
            return;

        const auto top = typeColumn.getY();
        const auto height = typeColumn.getHeight();

        g.setColour (wicky::ui::miami::radicalBlue.withAlpha (0.30f));
        g.fillRect (typeColumn.getRight() + columnGap / 2, top, 1, height);
        g.fillRect (complexityColumn.getRight() + columnGap / 2, top, 1, height);

        drawGroupTitle (g, "TYPE", typeTitle);
        drawGroupTitle (g, "COMPLEXITY", complexityTitle);
        drawGroupTitle (g, "VOICING", voicingTitle);
        drawGroupTitle (g, "PITCH OFFSET", pitchTitle);

        g.setColour (wicky::ui::miami::radicalBlue.withAlpha (0.55f));
        g.drawRoundedRectangle (voicingBox.toFloat(), 6.0f, 1.0f);
    }

    void resized() override
    {
        headerHeight = juce::jlimit (24, 44, getHeight() / 12);

        auto area = getLocalBounds();

        area.removeFromTop (headerHeight);
        area.reduce (12, 10);

        if (area.getWidth() <= 0 || area.getHeight() <= 0)
            return;

        columnGap = juce::jmax (8, area.getWidth() / 48);
        const auto columnWidth = juce::jmax (0, (area.getWidth() - columnGap * 2) / 3);

        typeColumn = area.removeFromLeft (columnWidth);
        area.removeFromLeft (columnGap);
        complexityColumn = area.removeFromLeft (columnWidth);
        area.removeFromLeft (columnGap);
        voicingColumn = area;

        const auto titleHeight = juce::jlimit (16, 22, typeColumn.getHeight() / 14);

        // ---------------------------------------------------------------------
        // Type column
        // ---------------------------------------------------------------------
        auto typeContent = typeColumn;
        typeTitle = typeContent.removeFromTop (titleHeight);
        typeContent.reduce (8, 4);

        juce::FlexBox typeFlex;
        typeFlex.flexDirection = juce::FlexBox::Direction::column;
        typeFlex.justifyContent = juce::FlexBox::JustifyContent::spaceAround;

        for (auto* button : typeButtons)
            typeFlex.items.add (juce::FlexItem (*button)
                                    .withFlex (1.0f)
                                    .withMargin (juce::FlexItem::Margin (4, 0, 4, 0)));

        typeFlex.performLayout (typeContent);

        // ---------------------------------------------------------------------
        // Complexity grid: 2 columns x 6 rows, row-major. The final cell is
        // intentionally empty (11 choices).
        // ---------------------------------------------------------------------
        auto complexityContent = complexityColumn;
        complexityTitle = complexityContent.removeFromTop (titleHeight);
        complexityContent.reduce (4, 4);

        juce::Grid complexityGrid;
        complexityGrid.justifyContent = juce::Grid::JustifyContent::stretch;
        complexityGrid.alignItems = juce::Grid::AlignItems::stretch;

        using Track = juce::Grid::TrackInfo;
        complexityGrid.templateColumns = { Track (juce::Grid::Fr (1)), Track (juce::Grid::Fr (1)) };
        complexityGrid.templateRows = { Track (juce::Grid::Fr (1)), Track (juce::Grid::Fr (1)),
                                        Track (juce::Grid::Fr (1)), Track (juce::Grid::Fr (1)),
                                        Track (juce::Grid::Fr (1)), Track (juce::Grid::Fr (1)) };
        complexityGrid.setGap (juce::Grid::Px (5));

        for (auto* button : complexityButtons)
            complexityGrid.items.add (juce::GridItem (*button));

        complexityGrid.performLayout (complexityContent);

        // ---------------------------------------------------------------------
        // Voicing + Pitch Offset column
        // ---------------------------------------------------------------------
        auto voicingContent = voicingColumn;
        voicingTitle = voicingContent.removeFromTop (titleHeight);
        voicingContent.reduce (8, 4);

        const auto pitchHeight = juce::jlimit (44, 62, voicingContent.getHeight() / 4);
        auto pitchBlock = voicingContent.removeFromBottom (pitchHeight);
        voicingContent.removeFromBottom (6);

        voicingBox = voicingContent;

        juce::FlexBox voicingFlex;
        voicingFlex.flexDirection = juce::FlexBox::Direction::column;
        voicingFlex.justifyContent = juce::FlexBox::JustifyContent::spaceAround;

        for (auto* button : voicingButtons)
            voicingFlex.items.add (juce::FlexItem (*button)
                                       .withFlex (1.0f)
                                       .withMargin (juce::FlexItem::Margin (3, 0, 3, 0)));

        voicingFlex.performLayout (voicingBox.reduced (10, 8));

        pitchTitle = pitchBlock.removeFromTop (juce::jmin (16, pitchBlock.getHeight()));

        auto row = pitchBlock.withSizeKeepingCentre (
            juce::jmin (pitchBlock.getWidth(), 190),
            juce::jmin (pitchBlock.getHeight(), 30));

        const auto buttonWidth = juce::jmin (38, juce::jmax (24, row.getWidth() / 4));
        pitchDecButton.setBounds (row.removeFromLeft (buttonWidth));
        row.removeFromLeft (6);
        pitchIncButton.setBounds (row.removeFromRight (buttonWidth));
        row.removeFromRight (6);
        pitchValueLabel.setBounds (row);
    }

private:
    //==========================================================================
    void createButtonGroup (juce::OwnedArray<juce::TextButton>& group,
                            const juce::String& parameterID,
                            int radioGroupId)
    {
        const auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
            state.getParameter (parameterID));

        jassert (choice != nullptr);

        if (choice == nullptr)
            return;

        for (const auto& label : choice->choices)
        {
            auto* button = group.add (std::make_unique<juce::TextButton> (label));

            button->setClickingTogglesState (true);
            button->setRadioGroupId (radioGroupId);
            button->setColour (juce::TextButton::buttonColourId, wicky::ui::miami::controlFace);
            button->setColour (juce::TextButton::buttonOnColourId, wicky::ui::miami::radicalBlue);
            button->setColour (juce::TextButton::textColourOffId, wicky::ui::miami::fluorescentPink);
            button->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            button->setMouseCursor (juce::MouseCursor::PointingHandCursor);

            addAndMakeVisible (button);
        }
    }

    void configurePitchControls()
    {
        for (auto* button : { &pitchDecButton, &pitchIncButton })
        {
            button->setColour (juce::TextButton::buttonColourId, wicky::ui::miami::controlFace);
            button->setColour (juce::TextButton::textColourOffId, wicky::ui::miami::fluorescentPink);
            button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            addAndMakeVisible (button);
        }

        pitchDecButton.setButtonText ("-");
        pitchIncButton.setButtonText ("+");
        pitchDecButton.setName ("Decrease pitch offset");
        pitchIncButton.setName ("Increase pitch offset");

        pitchDecButton.onClick = [this] { nudgePitch (-1); };
        pitchIncButton.onClick = [this] { nudgePitch (1); };

        pitchValueLabel.setJustificationType (juce::Justification::centred);
        pitchValueLabel.setColour (juce::Label::textColourId, wicky::ui::miami::brightMint);
        pitchValueLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        pitchValueLabel.setInterceptsMouseClicks (false, false);
        pitchValueLabel.setText ("0", juce::dontSendNotification);
        addAndMakeVisible (pitchValueLabel);
    }

    void nudgePitch (int delta)
    {
        if (pitchParameter == nullptr || pitchRawParameter == nullptr)
            return;

        const auto current = juce::roundToInt (pitchRawParameter->load (std::memory_order_relaxed));
        const auto next = juce::jlimit (boilerplate::parameters::pitchOffsetMin,
                                        boilerplate::parameters::pitchOffsetMax,
                                        current + delta);

        if (next == current)
            return;

        pitchParameter->beginChangeGesture();
        pitchParameter->setValueNotifyingHost (
            pitchParameter->convertTo0to1 (static_cast<float> (next)));
        pitchParameter->endChangeGesture();

        lastShownPitch = std::numeric_limits<int>::max();
        updatePitchLabel();
    }

    void updatePitchLabel()
    {
        if (pitchRawParameter == nullptr)
            return;

        const auto value = juce::roundToInt (pitchRawParameter->load (std::memory_order_relaxed));

        if (value == lastShownPitch)
            return;

        lastShownPitch = value;
        pitchValueLabel.setText (value > 0 ? "+" + juce::String (value)
                                           : juce::String (value),
                                 juce::dontSendNotification);
    }

    void timerCallback() override
    {
        updatePitchLabel();
    }

    static void drawGroupTitle (juce::Graphics& g,
                                const juce::String& text,
                                juce::Rectangle<int> area)
    {
        if (area.isEmpty())
            return;

        g.setColour (wicky::ui::miami::highlighterYellow.withAlpha (0.92f));
        const auto fontSize = juce::jlimit (9.5f, 13.0f,
                                            static_cast<float> (area.getWidth()) * 0.065f);
        g.setFont (juce::Font (juce::FontOptions (fontSize, juce::Font::bold))
                       .withExtraKerningFactor (0.14f));
        g.drawText (text, area, juce::Justification::centred, false);
    }

    //==========================================================================
    juce::AudioProcessorValueTreeState& state;

    juce::OwnedArray<juce::TextButton> typeButtons;
    juce::OwnedArray<juce::TextButton> complexityButtons;
    juce::OwnedArray<juce::TextButton> voicingButtons;

    juce::TextButton pitchDecButton;
    juce::TextButton pitchIncButton;
    juce::Label pitchValueLabel;

    std::unique_ptr<wicky::ui::ButtonGroupAttachment> typeAttachment;
    std::unique_ptr<wicky::ui::ButtonGroupAttachment> complexityAttachment;
    std::unique_ptr<wicky::ui::ButtonGroupAttachment> voicingAttachment;

    juce::RangedAudioParameter* pitchParameter = nullptr;
    std::atomic<float>* pitchRawParameter = nullptr;
    int lastShownPitch = std::numeric_limits<int>::max();

    juce::Rectangle<int> typeColumn, complexityColumn, voicingColumn;
    juce::Rectangle<int> typeTitle, complexityTitle, voicingTitle, pitchTitle;
    juce::Rectangle<int> voicingBox;

    int columnGap = 12;
    int headerHeight = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordSettingsPanel)
};
