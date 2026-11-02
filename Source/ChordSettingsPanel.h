#pragma once

#include <atomic>
#include <functional>
#include <limits>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "State/ParameterIDs.h"
#include "UI/OxaniumFonts.h"

//==============================================================================
namespace wicky::ui
{
    /** Miami Nights (Radical Neon) palette. */
    namespace miami
    {
        inline const juce::Colour midnightArcade    { 0xff121214 };
        inline const juce::Colour radicalBlue       { 0xff0055ff };
        inline const juce::Colour fluorescentPink   { 0xffff1493 };
        inline const juce::Colour highlighterYellow { 0xffeeee00 };
        inline const juce::Colour brightMint        { 0xff39ff14 };

        // Derived control surfaces, all built from the five core neon colours.
        inline const juce::Colour controlFace       { 0xff1a1a21 };
        inline const juce::Colour controlEdge       { 0xff0055ff };
        inline const juce::Colour textPrimary       { 0xfff2f2f7 };
        inline const juce::Colour textMuted         { 0xff8a8a99 };
        inline const juce::Colour keyWhite          { 0xffe9e9f2 };
        inline const juce::Colour keyBlack          { 0xff16161c };
        inline const juce::Colour keyLabel          { 0xff3a3a46 };
    }

    //==========================================================================
    /**
        A MidiKeyboardComponent that adds two pieces of Chordinator state on top
        of JUCE's stock keyboard:

        - the key currently selected for per-key recipe editing (fuchsia), and
        - the notes the host is currently holding (electric blue).

        Clicks still play through the processor, via the lock-free FIFO owned by
        PluginProcessor. All drawing happens on the message thread.
    */
    class ChordKeyboard final : public juce::MidiKeyboardComponent
    {
    public:
        explicit ChordKeyboard (juce::MidiKeyboardState& keyboardState)
            : MidiKeyboardComponent (keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
        {
            setScrollButtonsVisible (false);
            setOctaveForMiddleC (4);
            setAvailableRange (48, 84); // C3..C6
            setLowestVisibleKey (48);
            setVelocity (0.8f, false);
            setMidiChannel (1);

            setColour (whiteNoteColourId, miami::keyWhite);
            setColour (blackNoteColourId, miami::keyBlack);
            setColour (keySeparatorLineColourId, miami::midnightArcade);
            setColour (mouseOverKeyOverlayColourId, juce::Colours::transparentBlack);
            setColour (keyDownOverlayColourId, juce::Colours::transparentBlack);
            setColour (textLabelColourId, miami::keyLabel);
            setColour (shadowColourId, juce::Colours::transparentBlack);

            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void setSelectedNote (int midiNote)
        {
            midiNote = juce::jlimit (0, 127, midiNote);

            if (selectedNote == midiNote)
                return;

            selectedNote = midiNote;
            repaint();
        }

        int getSelectedNote() const noexcept { return selectedNote; }

        void setActiveNotes (juce::uint64 lowWord, juce::uint64 highWord)
        {
            if (activeLow == lowWord && activeHigh == highWord)
                return;

            activeLow = lowWord;
            activeHigh = highWord;
            repaint();
        }

        /** Called when the user clicks or drags onto a key. */
        std::function<void (int)> onNoteSelected;

    protected:
        bool mouseDownOnKey (int midiNoteNumber, const juce::MouseEvent&) override
        {
            if (onNoteSelected != nullptr)
                onNoteSelected (midiNoteNumber);

            return true; // Let JUCE play the note as well.
        }

        bool mouseDraggedToKey (int midiNoteNumber, const juce::MouseEvent&) override
        {
            if (onNoteSelected != nullptr)
                onNoteSelected (midiNoteNumber);

            return true;
        }

        void drawWhiteNote (int midiNoteNumber, juce::Graphics& g,
                            juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour lineColour, juce::Colour textColour) override
        {
            auto fill = findColour (whiteNoteColourId);

            if (isDown)
                fill = miami::highlighterYellow;
            else if (midiNoteNumber == selectedNote)
                fill = miami::fluorescentPink;
            else if (isActive (midiNoteNumber))
                fill = miami::radicalBlue.brighter (0.35f);
            else if (isOver)
                fill = fill.brighter (0.04f);

            g.setColour (fill);
            g.fillRect (area);

            // Thin separators between adjacent white keys, plus a closing
            // edge after the final key in the visible range.
            g.setColour (lineColour);
            g.fillRect (area.withWidth (1.0f));

            if (midiNoteNumber == getRangeEnd())
                g.fillRect (area.expanded (1.0f, 0.0f).removeFromRight (1.0f));

            g.drawLine (area.getX(), area.getBottom() - 1.0f,
                        area.getRight(), area.getBottom() - 1.0f, 1.0f);

            const auto label = getWhiteNoteText (midiNoteNumber);

            if (label.isNotEmpty())
            {
                auto labelArea = area.removeFromBottom (juce::jmin (area.getHeight() * 0.30f, 12.0f));
                g.setColour (isDown || midiNoteNumber == selectedNote
                                 ? juce::Colours::black
                                 : textColour);
                g.setFont (boilerplate::ui::fonts::oxanium (
                    juce::jlimit (7.0f, 9.5f, area.getWidth() * 0.32f), 700.0f));
                g.drawText (label, labelArea, juce::Justification::centred, false);
            }
        }

        void drawBlackNote (int midiNoteNumber, juce::Graphics& g,
                            juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour noteFillColour) override
        {
            auto fill = noteFillColour;

            if (isDown)
                fill = miami::highlighterYellow.darker (0.25f);
            else if (midiNoteNumber == selectedNote)
                fill = miami::fluorescentPink;
            else if (isActive (midiNoteNumber))
                fill = miami::radicalBlue;
            else if (isOver)
                fill = fill.brighter (0.20f);

            g.setColour (fill);
            g.fillRect (area);

            g.setColour (miami::radicalBlue.withAlpha (0.80f));
            g.drawRect (area.reduced (0.5f), 0.8f);
        }

    private:
        bool isActive (int midiNoteNumber) const noexcept
        {
            if (! juce::isPositiveAndBelow (midiNoteNumber, 128))
                return false;

            const auto word = midiNoteNumber < 64 ? activeLow : activeHigh;
            return (word & (static_cast<juce::uint64> (1) << (midiNoteNumber & 63))) != 0;
        }

        int selectedNote = 60;
        juce::uint64 activeLow = 0;
        juce::uint64 activeHigh = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordKeyboard)
    };

    //==========================================================================
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

            // Belt and braces: the radio group already un-clicks its siblings,
            // but an explicit pass makes the exclusivity invariant obvious and
            // survives any future look-and-feel/button changes.
            for (int i = 0; i < buttons.size(); ++i)
                if (auto* button = buttons[i])
                    button->setToggleState (i == index, juce::dontSendNotification);

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
    The chord-specific control surface: Type, Complexity, Voicing, the per-key
    Pitch Offset, the key-map controls, and the virtual MIDI keyboard.

    The panel owns no DSP; it only writes APVTS parameters through the
    attachments. Keyboard notes cross to the audio thread through the
    processor's lock-free FIFO.
*/
class ChordSettingsPanel final : public juce::Component,
                                 private juce::Timer,
                                 private juce::MidiKeyboardState::Listener
{
public:
    explicit ChordSettingsPanel (PluginProcessor& processorToUse)
        : processor (processorToUse),
          state (processorToUse.getValueTreeState()),
          chordKeyboard (processorToUse.getKeyboardState()),
          pitchParameter (state.getParameter (boilerplate::parameters::pitchOffset)),
          pitchRawParameter (state.getRawParameterValue (boilerplate::parameters::pitchOffset))
    {
        setName ("Chord Settings");
        setDescription ("Chord type, complexity, voicing, per-key mapping and pitch offset");

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
        configureKeyMap();
        configureKeyboard();

        processor.getKeyboardState().addListener (this);

        updatePitchLabel();
        updateKeyMapStatus();
        updateActiveNotes();
        startTimerHz (24);
    }

    ~ChordSettingsPanel() override
    {
        stopTimer();

        // Send note-offs for anything still held when the editor closes so the
        // processor cannot be left with an orphaned chord.
        processor.getKeyboardState().allNotesOff (1);
        processor.getKeyboardState().removeListener (this);
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
            return;

        g.setColour (wicky::ui::miami::midnightArcade);
        g.fillRoundedRectangle (bounds, 8.0f);

        g.setColour (wicky::ui::miami::radicalBlue.withAlpha (0.90f));
        g.drawRoundedRectangle (bounds, 8.0f, 1.5f);

        auto header = bounds.removeFromTop (static_cast<float> (headerHeight));

        juce::Path headerShape;
        headerShape.addRoundedRectangle (header.getX(), header.getY(),
                                         header.getWidth(), header.getHeight(),
                                         8.0f, 8.0f, true, true, false, false);

        g.setGradientFill (juce::ColourGradient (
            wicky::ui::miami::radicalBlue, header.getX(), header.getY(),
            wicky::ui::miami::fluorescentPink, header.getRight(), header.getY(),
            false));
        g.fillPath (headerShape);

        // Cyberpunk yellow rule under the header, then a toxic-green cap on
        // the right for the "live" state.
        g.setColour (wicky::ui::miami::highlighterYellow);
        g.fillRect (header.getX() + 14.0f, header.getBottom() - 2.0f,
                    juce::jmin (150.0f, header.getWidth() - 28.0f), 2.0f);

        g.setColour (juce::Colours::white);
        const auto titleSize = juce::jlimit (13.0f, 18.0f,
                                             static_cast<float> (headerHeight) * 0.46f);
        g.setFont (boilerplate::ui::fonts::oxanium (titleSize, 700.0f)
                       .withExtraKerningFactor (0.12f));
        g.drawText ("CHORD MATRIX", header.reduced (14.0f, 0.0f),
                    juce::Justification::centredLeft, false);

        g.setColour (anyInputActive
                         ? wicky::ui::miami::brightMint
                         : juce::Colours::white.withAlpha (0.62f));
        g.setFont (boilerplate::ui::fonts::oxanium (10.0f, 700.0f)
                       .withExtraKerningFactor (0.16f));
        g.drawText (anyInputActive ? "LIVE" : "IDLE", header.reduced (14.0f, 0.0f),
                    juce::Justification::centredRight, false);

        if (gridArea.isEmpty())
            return;

        const auto top = gridArea.getY();
        const auto height = gridArea.getHeight();

        g.setColour (wicky::ui::miami::radicalBlue.withAlpha (0.32f));
        g.fillRect (typeColumn.getRight() + columnGap / 2, top, 1, height);
        g.fillRect (complexityColumn.getRight() + columnGap / 2, top, 1, height);

        drawGroupTitle (g, "TYPE", typeTitle);
        drawGroupTitle (g, "COMPLEXITY", complexityTitle);
        drawGroupTitle (g, "VOICING", voicingTitle);
        drawGroupTitle (g, "PITCH OFFSET", pitchTitle);

        g.setColour (wicky::ui::miami::radicalBlue.withAlpha (0.60f));
        g.drawRoundedRectangle (voicingBox.toFloat(), 6.0f, 1.0f);

        if (! keyMapArea.isEmpty())
        {
            g.setColour (wicky::ui::miami::fluorescentPink.withAlpha (0.35f));
            g.fillRect (keyMapArea.getX(), keyMapArea.getY() - 4,
                        keyMapArea.getWidth(), 1);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (keyboardArea.isEmpty())
            return;

        g.setColour (wicky::ui::miami::fluorescentPink.withAlpha (0.90f));
        g.drawRoundedRectangle (keyboardArea.toFloat().expanded (1.0f), 4.0f, 1.5f);

        g.setColour (wicky::ui::miami::radicalBlue.withAlpha (0.45f));
        g.fillRect (keyboardArea.getX(), keyboardArea.getY() - 4,
                    keyboardArea.getWidth(), 1);
    }

    void resized() override
    {
        headerHeight = juce::jlimit (28, 38, getHeight() / 10);

        auto area = getLocalBounds();

        area.removeFromTop (headerHeight);
        area.reduce (10, 8);

        if (area.getWidth() <= 0 || area.getHeight() <= 0)
            return;

        // Keyboard first, then the key-map toolbar, then the control grid.
        const auto keyboardHeight = juce::jlimit (54, 76, area.getHeight() / 5);
        keyboardArea = area.removeFromBottom (keyboardHeight);
        area.removeFromBottom (6);

        const auto keyMapHeight = juce::jlimit (24, 30, area.getHeight() / 12);
        keyMapArea = area.removeFromBottom (keyMapHeight);
        area.removeFromBottom (8);

        gridArea = area;

        columnGap = juce::jmax (8, area.getWidth() / 56);

        const auto contentWidth = juce::jmax (0, area.getWidth() - columnGap * 2);
        const auto typeWidth = juce::roundToInt (static_cast<float> (contentWidth) * 0.235f);
        const auto complexityWidth = juce::roundToInt (static_cast<float> (contentWidth) * 0.455f);

        typeColumn = area.removeFromLeft (typeWidth);
        area.removeFromLeft (columnGap);
        complexityColumn = area.removeFromLeft (complexityWidth);
        area.removeFromLeft (columnGap);
        voicingColumn = area;

        const auto titleHeight = juce::jlimit (16, 22, gridArea.getHeight() / 12);

        // ---------------------------------------------------------------------
        // Type column: four strictly exclusive radio buttons.
        // ---------------------------------------------------------------------
        auto typeContent = typeColumn;
        typeTitle = typeContent.removeFromTop (titleHeight);
        typeContent.reduce (6, 4);

        if (! typeContent.isEmpty())
        {
            juce::FlexBox typeFlex;
            typeFlex.flexDirection = juce::FlexBox::Direction::column;
            typeFlex.justifyContent = juce::FlexBox::JustifyContent::spaceAround;

            for (auto* button : typeButtons)
                typeFlex.items.add (juce::FlexItem (*button)
                                        .withFlex (1.0f)
                                        .withMargin (juce::FlexItem::Margin (3, 0, 3, 0)));

            typeFlex.performLayout (typeContent);
        }

        // ---------------------------------------------------------------------
        // Complexity: 2 columns x 6 rows. The eleventh choice spans the whole
        // final row, which uses the otherwise-empty twelfth cell and keeps the
        // block symmetrical.
        // ---------------------------------------------------------------------
        auto complexityContent = complexityColumn;
        complexityTitle = complexityContent.removeFromTop (titleHeight);
        complexityContent.reduce (3, 4);

        if (! complexityContent.isEmpty() && complexityButtons.size() > 0)
        {
            juce::Grid complexityGrid;
            complexityGrid.justifyContent = juce::Grid::JustifyContent::stretch;
            complexityGrid.alignItems = juce::Grid::AlignItems::stretch;

            using Track = juce::Grid::TrackInfo;
            complexityGrid.templateColumns = { Track (juce::Grid::Fr (1)),
                                               Track (juce::Grid::Fr (1)) };
            complexityGrid.templateRows = { Track (juce::Grid::Fr (1)),
                                            Track (juce::Grid::Fr (1)),
                                            Track (juce::Grid::Fr (1)),
                                            Track (juce::Grid::Fr (1)),
                                            Track (juce::Grid::Fr (1)),
                                            Track (juce::Grid::Fr (1)) };
            complexityGrid.setGap (juce::Grid::Px (5));

            const auto count = complexityButtons.size();

            for (int i = 0; i < count; ++i)
            {
                auto item = juce::GridItem (*complexityButtons[i]);

                if (count == 11 && i == 10)
                    item.withArea (6, 1, 7, 3); // row 6, columns 1-2
                else
                    item.withArea (i / 2 + 1, (i % 2) + 1);

                complexityGrid.items.add (item);
            }

            complexityGrid.performLayout (complexityContent);
        }

        // ---------------------------------------------------------------------
        // Voicing + Pitch Offset column.
        // ---------------------------------------------------------------------
        auto voicingContent = voicingColumn;
        voicingTitle = voicingContent.removeFromTop (titleHeight);
        voicingContent.reduce (6, 4);

        const auto pitchHeight = juce::jlimit (44, 62, voicingContent.getHeight() / 4);
        auto pitchBlock = voicingContent.removeFromBottom (pitchHeight);
        voicingContent.removeFromBottom (6);

        voicingBox = voicingContent;

        if (! voicingBox.isEmpty())
        {
            juce::FlexBox voicingFlex;
            voicingFlex.flexDirection = juce::FlexBox::Direction::column;
            voicingFlex.justifyContent = juce::FlexBox::JustifyContent::spaceAround;

            for (auto* button : voicingButtons)
                voicingFlex.items.add (juce::FlexItem (*button)
                                           .withFlex (1.0f)
                                           .withMargin (juce::FlexItem::Margin (3, 0, 3, 0)));

            voicingFlex.performLayout (voicingBox.reduced (8, 6));
        }

        pitchTitle = pitchBlock.removeFromTop (juce::jmin (16, pitchBlock.getHeight()));

        auto row = pitchBlock.withSizeKeepingCentre (
            juce::jmin (pitchBlock.getWidth(), 180),
            juce::jmin (pitchBlock.getHeight(), 28));

        const auto buttonWidth = juce::jmin (36, juce::jmax (22, row.getWidth() / 4));
        pitchDecButton.setBounds (row.removeFromLeft (buttonWidth));
        row.removeFromLeft (6);
        pitchIncButton.setBounds (row.removeFromRight (buttonWidth));
        row.removeFromRight (6);
        pitchValueLabel.setBounds (row);

        // ---------------------------------------------------------------------
        // Key-map toolbar.
        // ---------------------------------------------------------------------
        auto keyRow = keyMapArea.withSizeKeepingCentre (
            keyMapArea.getWidth(), juce::jmin (24, keyMapArea.getHeight()));

        keyMapTitle.setBounds (keyRow.removeFromLeft (juce::jmin (54, keyRow.getWidth())));
        keySelector.setBounds (keyRow.removeFromLeft (juce::jmin (128, keyRow.getWidth())));

        clearKeyButton.setBounds (keyRow.removeFromRight (juce::jmin (52, keyRow.getWidth())));
        keyRow.removeFromRight (6);
        assignKeyButton.setBounds (keyRow.removeFromRight (juce::jmin (60, keyRow.getWidth())));
        keyRow.removeFromRight (6);
        loadKeyButton.setBounds (keyRow.removeFromRight (juce::jmin (46, keyRow.getWidth())));
        keyRow.removeFromRight (10);
        keyStatusLabel.setBounds (keyRow);

        // ---------------------------------------------------------------------
        // Virtual keyboard: exactly 22 white keys (C3..C6) across the width.
        // ---------------------------------------------------------------------
        chordKeyboard.setBounds (keyboardArea);

        if (keyboardArea.getWidth() > 0)
            chordKeyboard.setKeyWidth (juce::jmax (12.0f,
                static_cast<float> (keyboardArea.getWidth()) / 22.0f));
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

            // Radio groups are unique per column: TYPE, COMPLEXITY and VOICING
            // can never interfere with one another.
            button->setClickingTogglesState (true);
            button->setRadioGroupId (radioGroupId, juce::dontSendNotification);
            button->setColour (juce::TextButton::buttonColourId,
                               wicky::ui::miami::controlFace);
            button->setColour (juce::TextButton::buttonOnColourId,
                               wicky::ui::miami::radicalBlue);
            button->setColour (juce::TextButton::textColourOffId,
                               wicky::ui::miami::fluorescentPink);
            button->setColour (juce::TextButton::textColourOnId,
                               juce::Colours::white);
            button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            button->setName (label);

            addAndMakeVisible (button);
        }
    }

    void configurePitchControls()
    {
        for (auto* button : { &pitchDecButton, &pitchIncButton })
        {
            button->setColour (juce::TextButton::buttonColourId,
                               wicky::ui::miami::controlFace);
            button->setColour (juce::TextButton::textColourOffId,
                               wicky::ui::miami::fluorescentPink);
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
        pitchValueLabel.setFont (boilerplate::ui::fonts::oxanium (15.0f, 700.0f));
        pitchValueLabel.setInterceptsMouseClicks (false, false);
        pitchValueLabel.setText ("0", juce::dontSendNotification);
        addAndMakeVisible (pitchValueLabel);
    }

    void configureKeyMap()
    {
        keyMapTitle.setText ("KEY MAP", juce::dontSendNotification);
        keyMapTitle.setFont (boilerplate::ui::fonts::oxanium (10.0f, 700.0f)
                                 .withExtraKerningFactor (0.12f));
        keyMapTitle.setColour (juce::Label::textColourId, wicky::ui::miami::highlighterYellow);
        keyMapTitle.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        keyMapTitle.setJustificationType (juce::Justification::centredLeft);
        keyMapTitle.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (keyMapTitle);

        for (int note = 0; note < 128; ++note)
            keySelector.addItem (juce::MidiMessage::getMidiNoteName (note, true, true, 4)
                                     + "  (" + juce::String (note) + ")",
                                 note + 1);

        keySelector.setSelectedId (61, juce::dontSendNotification); // C4 = note 60
        keySelector.setColour (juce::ComboBox::backgroundColourId,
                               wicky::ui::miami::controlFace);
        keySelector.setColour (juce::ComboBox::textColourId,
                               wicky::ui::miami::fluorescentPink);
        keySelector.setColour (juce::ComboBox::outlineColourId,
                               wicky::ui::miami::radicalBlue.withAlpha (0.65f));
        keySelector.setColour (juce::ComboBox::arrowColourId,
                               wicky::ui::miami::highlighterYellow);
        keySelector.setColour (juce::ComboBox::focusedOutlineColourId,
                               wicky::ui::miami::brightMint);
        keySelector.setName ("Key map note");
        keySelector.setDescription ("Choose the MIDI note whose chord recipe you want to edit");
        keySelector.onChange = [this]
        {
            selectedKey = juce::jlimit (0, 127, keySelector.getSelectedId() - 1);
            chordKeyboard.setSelectedNote (selectedKey);
            updateKeyMapStatus();
        };
        addAndMakeVisible (keySelector);

        keyStatusLabel.setFont (boilerplate::ui::fonts::oxanium (10.5f, 700.0f));
        keyStatusLabel.setMinimumHorizontalScale (0.65f);
        keyStatusLabel.setJustificationType (juce::Justification::centredLeft);
        keyStatusLabel.setInterceptsMouseClicks (false, false);
        keyStatusLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (keyStatusLabel);

        styleSmallButton (loadKeyButton, "LOAD", false);
        styleSmallButton (assignKeyButton, "ASSIGN", true);
        styleSmallButton (clearKeyButton, "CLEAR", false);

        loadKeyButton.setName ("Load key recipe into the matrix");
        assignKeyButton.setName ("Assign the matrix to the selected key");
        clearKeyButton.setName ("Clear the selected key override");

        loadKeyButton.onClick = [this] { loadSelectedKeyRecipe(); };
        assignKeyButton.onClick = [this] { assignSelectedKeyRecipe(); };
        clearKeyButton.onClick = [this] { clearSelectedKeyRecipe(); };

        addAndMakeVisible (loadKeyButton);
        addAndMakeVisible (assignKeyButton);
        addAndMakeVisible (clearKeyButton);
    }

    void configureKeyboard()
    {
        chordKeyboard.setName ("Virtual keyboard");
        chordKeyboard.setDescription (
            "Play chords and select the key whose custom recipe is being edited");

        chordKeyboard.onNoteSelected = [this] (int note)
        {
            selectedKey = juce::jlimit (0, 127, note);
            keySelector.setSelectedId (selectedKey + 1, juce::dontSendNotification);
            chordKeyboard.setSelectedNote (selectedKey);
            updateKeyMapStatus();
        };

        addAndMakeVisible (chordKeyboard);
    }

    static void styleSmallButton (juce::TextButton& button,
                                  const juce::String& text,
                                  bool primary)
    {
        button.setButtonText (text);
        button.setColour (juce::TextButton::buttonColourId,
                          primary ? wicky::ui::miami::radicalBlue
                                  : wicky::ui::miami::controlFace);
        button.setColour (juce::TextButton::textColourOffId,
                          primary ? juce::Colours::white
                                  : wicky::ui::miami::fluorescentPink);
        button.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void assignSelectedKeyRecipe()
    {
        PluginProcessor::ChordRecipe recipe;
        recipe.chordType = readIntParameter (boilerplate::parameters::chordType);
        recipe.complexity = readIntParameter (boilerplate::parameters::complexity);
        recipe.voicing = readIntParameter (boilerplate::parameters::voicing);
        recipe.pitchOffset = readIntParameter (boilerplate::parameters::pitchOffset);

        processor.setKeyRecipe (selectedKey, recipe);
        updateKeyMapStatus();
    }

    void loadSelectedKeyRecipe()
    {
        if (! processor.hasKeyRecipe (selectedKey))
            return;

        const auto recipe = processor.getKeyRecipe (selectedKey);
        setIntParameter (boilerplate::parameters::chordType, recipe.chordType);
        setIntParameter (boilerplate::parameters::complexity, recipe.complexity);
        setIntParameter (boilerplate::parameters::voicing, recipe.voicing);
        setIntParameter (boilerplate::parameters::pitchOffset, recipe.pitchOffset);
        updateKeyMapStatus();
    }

    void clearSelectedKeyRecipe()
    {
        processor.clearKeyRecipe (selectedKey);
        updateKeyMapStatus();
    }

    void updateKeyMapStatus()
    {
        const auto hasOverride = processor.hasKeyRecipe (selectedKey);
        const auto recipe = processor.getKeyRecipe (selectedKey);

        juce::String status;

        if (hasOverride)
        {
            const auto pitch = recipe.pitchOffset > 0
                ? "+" + juce::String (recipe.pitchOffset)
                : juce::String (recipe.pitchOffset);

            status << "CUSTOM  "
                   << choiceLabel (boilerplate::parameters::chordType, recipe.chordType)
                   << "/"
                   << choiceLabel (boilerplate::parameters::complexity, recipe.complexity)
                   << "/"
                   << choiceLabel (boilerplate::parameters::voicing, recipe.voicing)
                   << "/" << pitch;

            keyStatusLabel.setColour (juce::Label::textColourId,
                                      wicky::ui::miami::brightMint);
        }
        else
        {
            status = "GLOBAL  (uses matrix)";
            keyStatusLabel.setColour (juce::Label::textColourId,
                                      wicky::ui::miami::highlighterYellow.withAlpha (0.85f));
        }

        if (keyStatusLabel.getText() != status)
            keyStatusLabel.setText (status, juce::dontSendNotification);

        if (loadKeyButton.isEnabled() != hasOverride)
            loadKeyButton.setEnabled (hasOverride);

        if (clearKeyButton.isEnabled() != hasOverride)
            clearKeyButton.setEnabled (hasOverride);
    }

    void updateActiveNotes()
    {
        const auto low = processor.getActiveInputNotes (0);
        const auto high = processor.getActiveInputNotes (1);

        chordKeyboard.setActiveNotes (low, high);

        const auto active = (low | high) != 0;

        if (active != anyInputActive)
        {
            anyInputActive = active;
            repaint();
        }
    }

    int readIntParameter (const char* id) const
    {
        if (auto* value = state.getRawParameterValue (id))
            return juce::roundToInt (value->load (std::memory_order_relaxed));

        return 0;
    }

    void setIntParameter (const char* id, int value)
    {
        if (auto* parameter = state.getParameter (id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (
                parameter->convertTo0to1 (static_cast<float> (value)));
            parameter->endChangeGesture();
        }
    }

    juce::String choiceLabel (const char* id, int index) const
    {
        if (const auto* choice = dynamic_cast<const juce::AudioParameterChoice*> (
                state.getParameter (id)))
            return choice->choices[juce::jlimit (0, choice->choices.size() - 1, index)];

        return {};
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

    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel,
                       int midiNoteNumber, float velocity) override
    {
        juce::ignoreUnused (midiChannel);
        processor.queueKeyboardNote (true, midiNoteNumber, velocity);
    }

    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel,
                        int midiNoteNumber, float velocity) override
    {
        juce::ignoreUnused (midiChannel, velocity);
        processor.queueKeyboardNote (false, midiNoteNumber, 0.0f);
    }

    void timerCallback() override
    {
        updatePitchLabel();
        updateKeyMapStatus();
        updateActiveNotes();
    }

    static void drawGroupTitle (juce::Graphics& g,
                                const juce::String& text,
                                juce::Rectangle<int> area)
    {
        if (area.isEmpty())
            return;

        g.setColour (wicky::ui::miami::highlighterYellow);
        const auto fontSize = juce::jlimit (9.5f, 12.0f,
                                            static_cast<float> (area.getWidth()) * 0.062f);
        g.setFont (boilerplate::ui::fonts::oxanium (fontSize, 700.0f)
                       .withExtraKerningFactor (0.14f));
        g.drawText (text, area, juce::Justification::centred, false);

        g.setColour (wicky::ui::miami::highlighterYellow.withAlpha (0.22f));
        g.fillRect (area.getX() + area.getWidth() / 4,
                    area.getBottom() - 1,
                    juce::jmax (1, area.getWidth() / 2), 1);
    }

    //==========================================================================
    PluginProcessor& processor;
    juce::AudioProcessorValueTreeState& state;

    juce::OwnedArray<juce::TextButton> typeButtons;
    juce::OwnedArray<juce::TextButton> complexityButtons;
    juce::OwnedArray<juce::TextButton> voicingButtons;

    juce::TextButton pitchDecButton;
    juce::TextButton pitchIncButton;
    juce::Label pitchValueLabel;

    juce::Label keyMapTitle;
    juce::ComboBox keySelector;
    juce::Label keyStatusLabel;
    juce::TextButton loadKeyButton;
    juce::TextButton assignKeyButton;
    juce::TextButton clearKeyButton;

    wicky::ui::ChordKeyboard chordKeyboard;

    std::unique_ptr<wicky::ui::ButtonGroupAttachment> typeAttachment;
    std::unique_ptr<wicky::ui::ButtonGroupAttachment> complexityAttachment;
    std::unique_ptr<wicky::ui::ButtonGroupAttachment> voicingAttachment;

    juce::RangedAudioParameter* pitchParameter = nullptr;
    std::atomic<float>* pitchRawParameter = nullptr;
    int lastShownPitch = std::numeric_limits<int>::max();
    int selectedKey = 60;
    bool anyInputActive = false;

    juce::Rectangle<int> typeColumn, complexityColumn, voicingColumn;
    juce::Rectangle<int> typeTitle, complexityTitle, voicingTitle, pitchTitle;
    juce::Rectangle<int> voicingBox;
    juce::Rectangle<int> keyMapArea, keyboardArea, gridArea;

    int columnGap = 12;
    int headerHeight = 36;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordSettingsPanel)
};
