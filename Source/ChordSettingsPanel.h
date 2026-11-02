#pragma once

#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

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
        A MidiKeyboardComponent that draws the plugin's real MIDI output.

        The MidiKeyboardState passed in is a display-only mirror: PluginProcessor
        publishes a lock-free mask of the notes in its final, chord-expanded
        output buffer and PluginEditor applies that mask to the state on the
        message thread. The component therefore lights up the generated chord
        shape, not the raw key that was clicked.

        Mouse input is intercepted before it can touch the display state. Clicks
        are forwarded through onRawNote exactly like hardware MIDI, so they run
        through the processor's normal chord engine and only become visible once
        they appear in the processed output.
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
            setVelocity (noteVelocity, false);
            setMidiChannel (1);

            // The display state is owned by the output, so the stock computer
            // keyboard mapping (which would write raw notes into that state)
            // is disabled. Hardware and UI mouse input both go through the
            // processor's raw MIDI bridge instead.
            clearKeyMappings();
            setWantsKeyboardFocus (false);

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

        void setScale (int root, int mode)
        {
            if (root == scaleRoot && mode == scaleMode)
                return;
            scaleRoot = root;
            scaleMode = mode;
            repaint();
        }

        /** Releases any note currently held by the mouse (used on teardown). */
        void releaseHeldRawNote()
        {
            stopRawNote();
        }

        /** Called when the user clicks or drags onto a key. */
        std::function<void (int)> onNoteSelected;

        /** Raw MIDI from the UI, before any chord expansion. */
        std::function<void (bool noteOn, int midiNote, float velocity)> onRawNote;

    protected:
        bool mouseDownOnKey (int midiNoteNumber, const juce::MouseEvent&) override
        {
            if (onNoteSelected != nullptr)
                onNoteSelected (midiNoteNumber);

            startRawNote (midiNoteNumber);

            // Suppress the stock state update: the display is fed exclusively
            // by the processor's processed-output mirror.
            return false;
        }

        bool mouseDraggedToKey (int midiNoteNumber, const juce::MouseEvent&) override
        {
            if (onNoteSelected != nullptr)
                onNoteSelected (midiNoteNumber);

            moveRawNote (midiNoteNumber);
            return false;
        }

        void mouseUpOnKey (int, const juce::MouseEvent&) override
        {
            stopRawNote();
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            // mouseUpOnKey is only called while the pointer is over a key, so
            // release here as well to cover releasing outside the keyboard.
            stopRawNote();
            MidiKeyboardComponent::mouseUp (e);
        }

        void drawWhiteNote (int midiNoteNumber, juce::Graphics& g,
                            juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour lineColour, juce::Colour textColour) override
        {
            auto fill = findColour (whiteNoteColourId);

            if (isDown)
                fill = miami::radicalBlue.brighter (0.35f);
            else if (midiNoteNumber == selectedNote)
                fill = miami::fluorescentPink;
            else if (isOver)
                fill = fill.brighter (0.04f);

            g.setColour (fill);
            g.fillRect (area);

            drawScaleMarker (midiNoteNumber, g, area);

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
                fill = miami::radicalBlue;
            else if (midiNoteNumber == selectedNote)
                fill = miami::fluorescentPink;
            else if (isOver)
                fill = fill.brighter (0.20f);

            g.setColour (fill);
            g.fillRect (area);

            g.setColour (miami::radicalBlue.withAlpha (0.80f));
            g.drawRect (area.reduced (0.5f), 0.8f);
            drawScaleMarker (midiNoteNumber, g, area);
        }

    private:
        int scaleRoot = 0, scaleMode = 0;
        void drawScaleMarker (int note, juce::Graphics& g, juce::Rectangle<float> area)
        {
            if (scaleMode == 0)
                return;
            const int major[] { 0, 2, 4, 5, 7, 9, 11 };
            const int minor[] { 0, 2, 3, 5, 7, 8, 10 };
            const auto pc = (note - scaleRoot + 144) % 12;
            const auto* degrees = scaleMode == 2 ? minor : major;
            for (int i = 0; i < 7; ++i)
                if (degrees[i] == pc)
                {
                    g.setColour (miami::brightMint.darker (0.2f));
                    g.fillEllipse (area.getCentreX() - 2.0f, area.getBottom() - 18.0f, 4.0f, 4.0f);
                    break;
                }
        }

        void startRawNote (int midiNoteNumber)
        {
            stopRawNote();

            if (! juce::isPositiveAndBelow (midiNoteNumber, 128)
                || onRawNote == nullptr)
                return;

            heldMouseNote = midiNoteNumber;
            onRawNote (true, heldMouseNote, noteVelocity);
        }

        void moveRawNote (int midiNoteNumber)
        {
            if (midiNoteNumber == heldMouseNote || ! juce::isPositiveAndBelow (midiNoteNumber, 128))
                return;

            stopRawNote();
            startRawNote (midiNoteNumber);
        }

        void stopRawNote()
        {
            if (heldMouseNote < 0)
                return;

            const auto note = heldMouseNote;
            heldMouseNote = -1;

            if (onRawNote != nullptr)
                onRawNote (false, note, 0.0f);
        }

        static constexpr float noteVelocity = 0.8f;

        int selectedNote = 60;
        int heldMouseNote = -1;

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

    //==========================================================================
    /**
        Factory/built-in preset store, backed by the APVTS ValueTree itself.

        Factory values are applied with parameter gestures. User preset files
        contain the processor's native state, including both recipe layers.
        Loading a factory preset retains mappings. Scale is session-owned and
        is preserved when loading either kind of preset.
    */
    class PresetManager final
    {
    public:
        explicit PresetManager (juce::AudioProcessorValueTreeState& stateToUse)
            : state (stateToUse)
        {
            using namespace boilerplate::parameters;

            // Build the factory baseline from each parameter's real default,
            // not from the live session. The editor can be created after a DAW
            // has restored a session, and preset names must stay deterministic.
            defaultState = juce::ValueTree (state.state.getType());
            for (auto* parameter : state.processor.getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                    defaultState.setProperty (ranged->paramID,
                        ranged->convertFrom0to1 (ranged->getDefaultValue()), nullptr);

            addPreset ("Init", {});

            addPreset ("Neon Pop",
                { { chordType, 0.0f }, { complexity, 0.0f }, { voicing, 1.0f },
                  { pitchOffset, 0.0f }, { spread, 32.0f }, { spreadMode, 0.0f },
                  { humanize, 18.0f }, { strum, 22.0f }, { strumMode, 0.0f },
                  { noteOffMode, 0.0f }, { power, 1.0f }, { bypass, 0.0f },
                  { gain, 0.0f } });

            addPreset ("Jazz 13ths",
                { { chordType, 2.0f }, { complexity, 7.0f }, { voicing, 1.0f },
                  { pitchOffset, 0.0f }, { spread, 46.0f }, { spreadMode, 1.0f },
                  { humanize, 28.0f }, { strum, 8.0f }, { strumMode, 0.0f },
                  { noteOffMode, 1.0f }, { power, 1.0f }, { bypass, 0.0f },
                  { gain, -2.0f } });

            addPreset ("Lo-Fi Keys",
                { { chordType, 1.0f }, { complexity, 2.0f }, { voicing, 2.0f },
                  { pitchOffset, 0.0f }, { spread, 24.0f }, { spreadMode, 0.0f },
                  { humanize, 48.0f }, { strum, 30.0f }, { strumMode, 1.0f },
                  { noteOffMode, 1.0f }, { power, 1.0f }, { bypass, 0.0f },
                  { gain, -5.0f } });

            addPreset ("Wide Dream",
                { { chordType, 0.0f }, { complexity, 4.0f }, { voicing, 0.0f },
                  { pitchOffset, 0.0f }, { spread, 82.0f }, { spreadMode, 1.0f },
                  { humanize, 22.0f }, { strum, 54.0f }, { strumMode, 1.0f },
                  { noteOffMode, 0.0f }, { power, 1.0f }, { bypass, 0.0f },
                  { gain, -3.0f } });

            addPreset ("Stab Matrix",
                { { chordType, 2.0f }, { complexity, 4.0f }, { voicing, 0.0f },
                  { pitchOffset, 0.0f }, { spread, 12.0f }, { spreadMode, 0.0f },
                  { humanize, 8.0f }, { strum, 0.0f }, { strumMode, 0.0f },
                  { noteOffMode, 0.0f }, { power, 1.0f }, { bypass, 0.0f },
                  { gain, 2.0f } });

            addPreset ("Dim Tension",
                { { chordType, 3.0f }, { complexity, 1.0f }, { voicing, 1.0f },
                  { pitchOffset, 0.0f }, { spread, 58.0f }, { spreadMode, 1.0f },
                  { humanize, 35.0f }, { strum, 40.0f }, { strumMode, 0.0f },
                  { noteOffMode, 1.0f }, { power, 1.0f }, { bypass, 0.0f },
                  { gain, -4.0f } });
        }

        explicit PresetManager (PluginProcessor& owner)
            : PresetManager (owner.getValueTreeState())
        {
            processor = &owner;
            auto files = getUserPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.wickychord");
            files.sort();
            for (const auto& file : files)
                presets.push_back ({ file.getFileNameWithoutExtension(), {}, file });
        }

        static juce::File getUserPresetDirectory()
        {
            return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                .getChildFile ("Wickman Wonderworks/Wicky Chordinator/Presets");
        }

        bool loadUserPreset (const juce::File& file)
        {
            for (size_t i = 0; i < presets.size(); ++i)
                if (presets[i].file == file)
                    return loadPreset (static_cast<int> (i));
            presets.push_back ({ file.getFileNameWithoutExtension(), {}, file });
            if (loadPreset (getNumPresets() - 1))
                return true;
            presets.pop_back();
            return false;
        }

        bool saveUserPreset (const juce::File& file)
        {
            if (processor == nullptr || file.getFileNameWithoutExtension().isEmpty())
                return false;
            juce::MemoryBlock data;
            processor->getStateInformation (data);
            if (file.getParentDirectory().createDirectory().failed())
                return false;
            juce::TemporaryFile temporary (file);
            if (! temporary.getFile().replaceWithData (data.getData(), data.getSize())
                || ! temporary.overwriteTargetFileWithTemporary())
                return false;
            for (size_t i = 0; i < presets.size(); ++i)
                if (presets[i].file == file)
                {
                    currentIndex = static_cast<int> (i);
                    return true;
                }
            presets.push_back ({ file.getFileNameWithoutExtension(), {}, file });
            currentIndex = getNumPresets() - 1;
            return true;
        }

        int getNumPresets() const noexcept
        {
            return static_cast<int> (presets.size());
        }

        juce::String getName (int index) const
        {
            if (juce::isPositiveAndBelow (index, getNumPresets()))
                return presets[static_cast<size_t> (index)].name;

            return {};
        }

        int getCurrentIndex() const noexcept { return currentIndex; }

        bool loadPreset (int index)
        {
            if (! juce::isPositiveAndBelow (index, getNumPresets()))
                return false;

            using namespace boilerplate::parameters;
            const auto root = state.getRawParameterValue (scaleRoot)->load();
            const auto mode = state.getRawParameterValue (scaleMode)->load();
            const auto setValue = [&] (const char* id, float value)
            {
                if (auto* parameter = state.getParameter (id))
                {
                    parameter->beginChangeGesture();
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
                    parameter->endChangeGesture();
                }
            };
            const auto& selected = presets[static_cast<size_t> (index)];
            if (selected.file != juce::File())
            {
                juce::MemoryBlock data;
                if (processor == nullptr || ! selected.file.loadFileAsData (data))
                    return false;
                auto tree = juce::ValueTree::readFromData (data.getData(), data.getSize());
                if (! tree.hasType (state.state.getType()))
                    return false;
                // Scale is a session preference, not a preset setting. Patch it
                // before recall to avoid even a transient scale change.
                for (const auto* id : { scaleRoot, scaleMode })
                {
                    auto child = tree.getChildWithProperty ("id", id);
                    if (! child.isValid())
                    {
                        child = juce::ValueTree ("PARAM");
                        child.setProperty ("id", id, nullptr);
                        tree.appendChild (child, nullptr);
                    }
                    child.setProperty ("value", juce::String (id) == scaleRoot ? root : mode, nullptr);
                }
                juce::MemoryBlock recalled;
                juce::MemoryOutputStream stream (recalled, false);
                tree.writeToStream (stream);
                processor->setStateInformation (recalled.getData(), static_cast<int> (recalled.getSize()));
                currentIndex = index;
                return true;
            }
            currentIndex = index;

            const auto presetState =
                presets[static_cast<size_t> (index)].state.createCopy();

            // Parameter gestures update the APVTS's native PARAM children.
            // Never replace it with a property-only pseudo-state.

            // Apply defaults and overrides through normal parameter gestures.
            for (int property = 0; property < presetState.getNumProperties(); ++property)
            {
                const auto propertyName = presetState.getPropertyName (property);
                if (propertyName.toString() == scaleRoot || propertyName.toString() == scaleMode)
                    continue;
                auto* parameter = state.getParameter (propertyName.toString());

                if (parameter == nullptr)
                    continue;

                const auto value = static_cast<float> (
                    static_cast<double> (presetState.getProperty (propertyName)));

                setValue (propertyName.toString().toRawUTF8(), value);
            }

            return true;
        }

        bool loadNext()
        {
            if (getNumPresets() == 0)
                return false;

            return loadPreset ((currentIndex + 1) % getNumPresets());
        }

        bool loadPrevious()
        {
            if (getNumPresets() == 0)
                return false;

            return loadPreset ((currentIndex + getNumPresets() - 1) % getNumPresets());
        }

    private:
        struct Preset
        {
            juce::String name;
            juce::ValueTree state;
            juce::File file;
        };

        void addPreset (juce::String name,
                        std::initializer_list<std::pair<const char*, float>> values)
        {
            auto tree = defaultState.createCopy();

            for (const auto& value : values)
                tree.setProperty (juce::Identifier (value.first), value.second, nullptr);

            presets.push_back ({ std::move (name), std::move (tree), {} });
        }

        juce::AudioProcessorValueTreeState& state;
        juce::ValueTree defaultState;
        std::vector<Preset> presets;
        PluginProcessor* processor = nullptr;
        int currentIndex = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
    };

    //==========================================================================
    /**
        Left column: the mutually exclusive Type, Complexity and Voicing radio
        groups plus the per-chord Pitch Offset stepper. No DSP lives here; all
        writes go through APVTS attachments on the message thread.
    */
    class ChordSettingsPanel final : public juce::Component,
                                     private juce::Timer
    {
    public:
        explicit ChordSettingsPanel (PluginProcessor& processorToUse)
            : state (processorToUse.getValueTreeState()),
              pitchParameter (state.getParameter (boilerplate::parameters::pitchOffset)),
              pitchRawParameter (state.getRawParameterValue (boilerplate::parameters::pitchOffset))
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
            typeAttachment = std::make_unique<ButtonGroupAttachment> (
                state, boilerplate::parameters::chordType, typeButtons);
            complexityAttachment = std::make_unique<ButtonGroupAttachment> (
                state, boilerplate::parameters::complexity, complexityButtons);
            voicingAttachment = std::make_unique<ButtonGroupAttachment> (
                state, boilerplate::parameters::voicing, voicingButtons);

            configurePitchControls();

            updatePitchLabel();
            startTimerHz (24);
        }

        ~ChordSettingsPanel() override
        {
            stopTimer();
        }

        //==========================================================================
        void paint (juce::Graphics& g) override
        {
            drawPanelFrame (g, "CHORD SETTINGS");

            if (gridArea.isEmpty())
                return;

            const auto top = gridArea.getY();
            const auto height = gridArea.getHeight();

            g.setColour (miami::radicalBlue.withAlpha (0.32f));
            g.fillRect (typeColumn.getRight() + columnGap / 2, top, 1, height);
            g.fillRect (complexityColumn.getRight() + columnGap / 2, top, 1, height);

            drawGroupTitle (g, "TYPE", typeTitle);
            drawGroupTitle (g, "COMPLEXITY", complexityTitle);
            drawGroupTitle (g, "VOICING", voicingTitle);
            drawGroupTitle (g, "PITCH OFFSET", pitchTitle);

            g.setColour (miami::radicalBlue.withAlpha (0.60f));
            g.drawRoundedRectangle (voicingBox.toFloat(), 6.0f, 1.0f);
        }

        void resized() override
        {
            headerHeight = juce::jlimit (28, 36, getHeight() / 10);

            auto area = getLocalBounds();
            area.removeFromTop (headerHeight);
            area.reduce (10, 8);

            if (area.getWidth() <= 0 || area.getHeight() <= 0)
                return;

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

            // Type column: four strictly exclusive radio buttons.
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

            // Complexity: 2 columns x 6 rows. The eleventh choice spans the whole
            // final row, which uses the otherwise-empty twelfth cell and keeps the
            // block symmetrical.
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

            // Voicing + Pitch Offset column.
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
                button->setColour (juce::TextButton::buttonColourId, miami::controlFace);
                button->setColour (juce::TextButton::buttonOnColourId, miami::radicalBlue);
                button->setColour (juce::TextButton::textColourOffId, miami::fluorescentPink);
                button->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
                button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
                button->setName (label);

                addAndMakeVisible (button);
            }
        }

        void configurePitchControls()
        {
            for (auto* button : { &pitchDecButton, &pitchIncButton })
            {
                button->setColour (juce::TextButton::buttonColourId, miami::controlFace);
                button->setColour (juce::TextButton::textColourOffId, miami::fluorescentPink);
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
            pitchValueLabel.setColour (juce::Label::textColourId, miami::brightMint);
            pitchValueLabel.setFont (boilerplate::ui::fonts::oxanium (15.0f, 700.0f));
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

        void drawPanelFrame (juce::Graphics& g, const juce::String& title) const
        {
            auto bounds = getLocalBounds().toFloat().reduced (0.5f);

            g.setColour (miami::midnightArcade);
            g.fillRoundedRectangle (bounds, 8.0f);

            g.setColour (miami::radicalBlue.withAlpha (0.90f));
            g.drawRoundedRectangle (bounds, 8.0f, 1.5f);

            auto header = bounds.removeFromTop (30.0f);

            juce::Path headerShape;
            headerShape.addRoundedRectangle (header.getX(), header.getY(),
                                             header.getWidth(), header.getHeight(),
                                             8.0f, 8.0f, true, true, false, false);

            g.setGradientFill (juce::ColourGradient (
                miami::radicalBlue, header.getX(), header.getY(),
                miami::fluorescentPink, header.getRight(), header.getY(),
                false));
            g.fillPath (headerShape);

            g.setColour (miami::highlighterYellow);
            g.fillRect (header.getX() + 14.0f, header.getBottom() - 2.0f,
                        juce::jmin (150.0f, header.getWidth() - 28.0f), 2.0f);

            g.setColour (juce::Colours::white);
            g.setFont (boilerplate::ui::fonts::oxanium (14.0f, 700.0f)
                           .withExtraKerningFactor (0.12f));
            g.drawText (title, header.reduced (14.0f, 0.0f),
                        juce::Justification::centredLeft, false);
        }

        static void drawGroupTitle (juce::Graphics& g,
                                    const juce::String& text,
                                    juce::Rectangle<int> area)
        {
            if (area.isEmpty())
                return;

            g.setColour (miami::highlighterYellow);
            const auto fontSize = juce::jlimit (9.5f, 12.0f,
                                                static_cast<float> (area.getWidth()) * 0.062f);
            g.setFont (boilerplate::ui::fonts::oxanium (fontSize, 700.0f)
                           .withExtraKerningFactor (0.14f));
            g.drawText (text, area, juce::Justification::centred, false);

            g.setColour (miami::highlighterYellow.withAlpha (0.22f));
            g.fillRect (area.getX() + area.getWidth() / 4,
                        area.getBottom() - 1,
                        juce::jmax (1, area.getWidth() / 2), 1);
        }

        //==========================================================================
        juce::AudioProcessorValueTreeState& state;

        juce::OwnedArray<juce::TextButton> typeButtons;
        juce::OwnedArray<juce::TextButton> complexityButtons;
        juce::OwnedArray<juce::TextButton> voicingButtons;

        juce::TextButton pitchDecButton;
        juce::TextButton pitchIncButton;
        juce::Label pitchValueLabel;

        std::unique_ptr<ButtonGroupAttachment> typeAttachment;
        std::unique_ptr<ButtonGroupAttachment> complexityAttachment;
        std::unique_ptr<ButtonGroupAttachment> voicingAttachment;

        juce::RangedAudioParameter* pitchParameter = nullptr;
        std::atomic<float>* pitchRawParameter = nullptr;
        int lastShownPitch = std::numeric_limits<int>::max();

        juce::Rectangle<int> typeColumn, complexityColumn, voicingColumn;
        juce::Rectangle<int> typeTitle, complexityTitle, voicingTitle, pitchTitle;
        juce::Rectangle<int> voicingBox;
        juce::Rectangle<int> gridArea;

        int columnGap = 12;
        int headerHeight = 32;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordSettingsPanel)
    };

    //==========================================================================
    /**
        Right column: Spread, Humanize and Strum rotary controls plus their
        matching mode selectors. The sliders use the shared Miami look, whose
        rotary renderer draws a radial dot indicator ring.
    */
    class GlobalSettingsPanel final : public juce::Component
    {
    public:
        explicit GlobalSettingsPanel (PluginProcessor& processorToUse)
            : state (processorToUse.getValueTreeState()),
              spreadAttachment (state, boilerplate::parameters::spread, spreadSlider),
              humanizeAttachment (state, boilerplate::parameters::humanize, humanizeSlider),
              strumAttachment (state, boilerplate::parameters::strum, strumSlider),
              spreadModeAttachment (state, boilerplate::parameters::spreadMode, spreadModeSelector),
              noteOffAttachment (state, boilerplate::parameters::noteOffMode, noteOffSelector),
              strumModeAttachment (state, boilerplate::parameters::strumMode, strumModeSelector),
              voicingStyleAttachment (state, boilerplate::parameters::voicingStyle, voicingStyleSelector),
              scaleRootAttachment (state, boilerplate::parameters::scaleRoot, scaleRootSelector),
              scaleModeAttachment (state, boilerplate::parameters::scaleMode, scaleModeSelector),
              humanizeTimingAttachment (state, boilerplate::parameters::humanizeTiming, humanizeTimingButton)
        {
            humanizeTimingButton.setButtonText ("Timing");
            humanizeTimingButton.setTooltip ("Include up to 10 ms of random note-on delay");
            addAndMakeVisible (humanizeTimingButton);
            setName ("Global Settings");
            setDescription ("Spread, humanize, strum and their mode selectors");

            configureRotary (spreadSlider, spreadLabel, "Spread",
                             "Opens the chord voicing across the keyboard");
            configureRotary (humanizeSlider, humanizeLabel, "Humanize",
                             "Randomizes velocity and, when Timing is enabled, note-on timing");
            configureRotary (strumSlider, strumLabel, "Strum",
                             "Delays successive chord tones for a strummed feel");

            configureSelector (spreadModeSelector, spreadModeLabel,
                               boilerplate::parameters::spreadMode, "Spread Mode");
            configureSelector (noteOffSelector, noteOffLabel,
                               boilerplate::parameters::noteOffMode, "Note Off");
            configureSelector (strumModeSelector, strumModeLabel,
                               boilerplate::parameters::strumMode, "Strum Mode");
            configureSelector (voicingStyleSelector, voicingStyleLabel,
                               boilerplate::parameters::voicingStyle, "Voicing");
            configureSelector (scaleRootSelector, scaleRootLabel,
                               boilerplate::parameters::scaleRoot, "Scale Root");
            configureSelector (scaleModeSelector, scaleModeLabel,
                               boilerplate::parameters::scaleMode, "Scale Mode");
        }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced (0.5f);

            g.setColour (miami::midnightArcade);
            g.fillRoundedRectangle (bounds, 8.0f);

            g.setColour (miami::fluorescentPink.withAlpha (0.85f));
            g.drawRoundedRectangle (bounds, 8.0f, 1.5f);

            auto header = bounds.removeFromTop (30.0f);

            juce::Path headerShape;
            headerShape.addRoundedRectangle (header.getX(), header.getY(),
                                             header.getWidth(), header.getHeight(),
                                             8.0f, 8.0f, true, true, false, false);

            g.setGradientFill (juce::ColourGradient (
                miami::fluorescentPink, header.getX(), header.getY(),
                miami::radicalBlue, header.getRight(), header.getY(),
                false));
            g.fillPath (headerShape);

            g.setColour (miami::brightMint);
            g.fillRect (header.getX() + 14.0f, header.getBottom() - 2.0f,
                        juce::jmin (150.0f, header.getWidth() - 28.0f), 2.0f);

            g.setColour (juce::Colours::white);
            g.setFont (boilerplate::ui::fonts::oxanium (14.0f, 700.0f)
                           .withExtraKerningFactor (0.12f));
            g.drawText ("GLOBAL SETTINGS", header.reduced (14.0f, 0.0f),
                        juce::Justification::centredLeft, false);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            area.removeFromTop (juce::jlimit (28, 34, getHeight() / 10));
            area.reduce (12, 10);

            if (area.getWidth() <= 0 || area.getHeight() <= 0)
                return;

            auto knobBlock = area.removeFromTop (juce::jlimit (66, 105, area.getHeight() / 3));
            area.removeFromTop (8);

            const auto knobWidth = knobBlock.getWidth() / 3;

            auto spreadCell = knobBlock.removeFromLeft (knobWidth);
            auto humanizeCell = knobBlock.removeFromLeft (knobWidth);
            auto strumCell = knobBlock;

            spreadLabel.setBounds (spreadCell.removeFromTop (18));
            humanizeLabel.setBounds (humanizeCell.removeFromTop (18));
            strumLabel.setBounds (strumCell.removeFromTop (18));

            spreadSlider.setBounds (spreadCell);
            humanizeTimingButton.setBounds (getWidth() - 95, 4, 90, 22);
            humanizeSlider.setBounds (humanizeCell);
            strumSlider.setBounds (strumCell);

            const auto selectorHeight = juce::jmax (18, area.getHeight() / 6);
            auto placeSelector = [&] (juce::Label& label, juce::ComboBox& box)
            {
                auto row = area.removeFromTop (selectorHeight);
                label.setBounds (row.removeFromLeft (juce::jmin (110, row.getWidth() / 2)));
                box.setBounds (row.reduced (2, 3));
            };

            placeSelector (spreadModeLabel, spreadModeSelector);
            placeSelector (noteOffLabel, noteOffSelector);
            placeSelector (strumModeLabel, strumModeSelector);
            placeSelector (voicingStyleLabel, voicingStyleSelector);
            placeSelector (scaleRootLabel, scaleRootSelector);
            placeSelector (scaleModeLabel, scaleModeSelector);
        }

    private:
        void configureRotary (juce::Slider& slider, juce::Label& label,
                              const juce::String& text,
                              const juce::String& description)
        {
            slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 18);
            slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                        juce::MathConstants<float>::pi * 2.8f,
                                        true);
            slider.setDoubleClickReturnValue (true, 0.0);
            slider.setColour (juce::Slider::textBoxTextColourId, miami::brightMint);
            slider.setColour (juce::Slider::textBoxBackgroundColourId, miami::midnightArcade);
            slider.setColour (juce::Slider::textBoxOutlineColourId,
                              miami::radicalBlue.withAlpha (0.55f));
            slider.getProperties().set ("dotRing", true);
            slider.setName (text);
            slider.setDescription (description);
            addAndMakeVisible (slider);

            label.setText (text.toUpperCase(), juce::dontSendNotification);
            label.setFont (boilerplate::ui::fonts::oxanium (10.5f, 700.0f)
                               .withExtraKerningFactor (0.14f));
            label.setColour (juce::Label::textColourId, miami::highlighterYellow);
            label.setJustificationType (juce::Justification::centred);
            label.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (label);
        }

        void configureSelector (juce::ComboBox& box, juce::Label& label,
                                const juce::String& parameterID,
                                const juce::String& labelText)
        {
            if (const auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
                    state.getParameter (parameterID)))
            {
                int itemId = 1;

                for (const auto& text : choice->choices)
                    box.addItem (text, itemId++);

                // The ComboBoxAttachment is constructed before this body runs,
                // so seed the visible selection here as well.
                box.setSelectedId (choice->getIndex() + 1,
                                   juce::dontSendNotification);
            }

            box.setColour (juce::ComboBox::backgroundColourId, miami::controlFace);
            box.setColour (juce::ComboBox::textColourId, miami::fluorescentPink);
            box.setColour (juce::ComboBox::outlineColourId,
                           miami::radicalBlue.withAlpha (0.65f));
            box.setColour (juce::ComboBox::arrowColourId, miami::highlighterYellow);
            box.setColour (juce::ComboBox::focusedOutlineColourId, miami::brightMint);
            box.setName (labelText);
            addAndMakeVisible (box);

            label.setText (labelText.toUpperCase(), juce::dontSendNotification);
            label.setFont (boilerplate::ui::fonts::oxanium (10.0f, 700.0f)
                               .withExtraKerningFactor (0.08f));
            label.setColour (juce::Label::textColourId, miami::textMuted);
            label.setJustificationType (juce::Justification::centredLeft);
            label.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (label);
        }

        juce::AudioProcessorValueTreeState& state;

        juce::Slider spreadSlider;
        juce::Slider humanizeSlider;
        juce::Slider strumSlider;

        juce::ComboBox spreadModeSelector;
        juce::ComboBox noteOffSelector;
        juce::ComboBox strumModeSelector;
        juce::ComboBox voicingStyleSelector, scaleRootSelector, scaleModeSelector;
        juce::Label voicingStyleLabel, scaleRootLabel, scaleModeLabel;
        juce::ToggleButton humanizeTimingButton;

        juce::Label spreadLabel;
        juce::Label humanizeLabel;
        juce::Label strumLabel;
        juce::Label spreadModeLabel;
        juce::Label noteOffLabel;
        juce::Label strumModeLabel;

        // Declared after the controls so the attachments are destroyed first.
        juce::AudioProcessorValueTreeState::SliderAttachment spreadAttachment;
        juce::AudioProcessorValueTreeState::SliderAttachment humanizeAttachment;
        juce::AudioProcessorValueTreeState::SliderAttachment strumAttachment;
        juce::AudioProcessorValueTreeState::ComboBoxAttachment spreadModeAttachment;
        juce::AudioProcessorValueTreeState::ComboBoxAttachment noteOffAttachment;
        juce::AudioProcessorValueTreeState::ComboBoxAttachment strumModeAttachment;
        juce::AudioProcessorValueTreeState::ComboBoxAttachment voicingStyleAttachment;
        juce::AudioProcessorValueTreeState::ComboBoxAttachment scaleRootAttachment;
        juce::AudioProcessorValueTreeState::ComboBoxAttachment scaleModeAttachment;
        juce::AudioProcessorValueTreeState::ButtonAttachment humanizeTimingAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalSettingsPanel)
    };

    //==========================================================================
    /**
        Per-key recipe toolbar. The map itself lives in PluginProcessor as a
        lock-free atomic array; this component is the message-thread editor for
        it and remains fully compatible with the original KeyRecipes state.
    */
    class KeyMapBar final : public juce::Component,
                            private juce::Timer
    {
    public:
        explicit KeyMapBar (PluginProcessor& processorToUse)
            : processor (processorToUse),
              state (processorToUse.getValueTreeState())
        {
            setName ("Key Map");

            keyMapTitle.setText ("KEY MAP", juce::dontSendNotification);
            keyMapTitle.setFont (boilerplate::ui::fonts::oxanium (10.0f, 700.0f)
                                     .withExtraKerningFactor (0.12f));
            keyMapTitle.setColour (juce::Label::textColourId, miami::highlighterYellow);
            keyMapTitle.setColour (juce::Label::backgroundColourId,
                                   juce::Colours::transparentBlack);
            keyMapTitle.setJustificationType (juce::Justification::centredLeft);
            keyMapTitle.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (keyMapTitle);

            for (int note = 0; note < 128; ++note)
                keySelector.addItem (juce::MidiMessage::getMidiNoteName (note, true, true, 4)
                                         + "  (" + juce::String (note) + ")",
                                     note + 1);

            keySelector.setSelectedId (61, juce::dontSendNotification); // C4 = note 60
            keySelector.setColour (juce::ComboBox::backgroundColourId, miami::controlFace);
            keySelector.setColour (juce::ComboBox::textColourId, miami::fluorescentPink);
            keySelector.setColour (juce::ComboBox::outlineColourId,
                                   miami::radicalBlue.withAlpha (0.65f));
            keySelector.setColour (juce::ComboBox::arrowColourId, miami::highlighterYellow);
            keySelector.setColour (juce::ComboBox::focusedOutlineColourId, miami::brightMint);
            keySelector.setName ("Key map note");
            keySelector.setDescription (
                "Choose the MIDI note whose chord recipe you want to edit");
            keySelector.onChange = [this]
            {
                selectedKey = juce::jlimit (0, 127, keySelector.getSelectedId() - 1);

                if (onSelectedKeyChanged != nullptr)
                    onSelectedKeyChanged (selectedKey);

                updateKeyMapStatus();
            };
            addAndMakeVisible (keySelector);

            keyStatusLabel.setFont (boilerplate::ui::fonts::oxanium (10.5f, 700.0f));
            keyStatusLabel.setMinimumHorizontalScale (0.65f);
            keyStatusLabel.setJustificationType (juce::Justification::centredLeft);
            keyStatusLabel.setInterceptsMouseClicks (false, false);
            keyStatusLabel.setColour (juce::Label::backgroundColourId,
                                      juce::Colours::transparentBlack);
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

            allOctavesButton.setButtonText ("All octaves");
            allOctavesButton.setToggleState (true, juce::dontSendNotification);
            allOctavesButton.onClick = [this] { updateKeyMapStatus(); };
            addAndMakeVisible (allOctavesButton);
            updateKeyMapStatus();
            startTimerHz (20);
        }

        ~KeyMapBar() override
        {
            stopTimer();
        }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced (0.5f);

            g.setColour (miami::controlFace.withAlpha (0.82f));
            g.fillRoundedRectangle (bounds, 6.0f);

            g.setColour (miami::radicalBlue.withAlpha (0.45f));
            g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
        }

        void resized() override
        {
            auto row = getLocalBounds().reduced (8, 2);

            keyMapTitle.setBounds (row.removeFromLeft (juce::jmin (58, row.getWidth())));
            keySelector.setBounds (row.removeFromLeft (juce::jmin (132, row.getWidth())));

            clearKeyButton.setBounds (row.removeFromRight (juce::jmin (54, row.getWidth())));
            row.removeFromRight (6);
            assignKeyButton.setBounds (row.removeFromRight (juce::jmin (62, row.getWidth())));
            row.removeFromRight (6);
            loadKeyButton.setBounds (row.removeFromRight (juce::jmin (48, row.getWidth())));
            row.removeFromRight (10);
            allOctavesButton.setBounds (row.removeFromLeft (juce::jmin (110, row.getWidth())));
            keyStatusLabel.setBounds (row);
        }

        int getSelectedKey() const noexcept { return selectedKey; }

        /** Updates the selector without echoing back to the keyboard. */
        void setSelectedKey (int note)
        {
            note = juce::jlimit (0, 127, note);

            if (selectedKey == note
                && keySelector.getSelectedId() == note + 1)
                return;

            selectedKey = note;
            keySelector.setSelectedId (selectedKey + 1, juce::dontSendNotification);
            updateKeyMapStatus();
        }

        std::function<void (int)> onSelectedKeyChanged;

    private:
        static void styleSmallButton (juce::TextButton& button,
                                      const juce::String& text,
                                      bool primary)
        {
            button.setButtonText (text);
            button.setColour (juce::TextButton::buttonColourId,
                              primary ? miami::radicalBlue : miami::controlFace);
            button.setColour (juce::TextButton::textColourOffId,
                              primary ? juce::Colours::white : miami::fluorescentPink);
            button.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void assignSelectedKeyRecipe()
        {
            PluginProcessor::ChordRecipe recipe;
            recipe.chordType = readIntParameter (boilerplate::parameters::chordType);
            recipe.complexity = readIntParameter (boilerplate::parameters::complexity);
            recipe.voicing = readIntParameter (boilerplate::parameters::voicing);
            recipe.pitchOffset = readIntParameter (boilerplate::parameters::pitchOffset);

            if (allOctavesButton.getToggleState())
                processor.setPitchClassRecipe (selectedKey % 12, recipe);
            else
                processor.setKeyRecipe (selectedKey, recipe);
            updateKeyMapStatus();
        }

        void loadSelectedKeyRecipe()
        {
            const auto all = allOctavesButton.getToggleState();
            if (all ? ! processor.hasPitchClassRecipe (selectedKey % 12)
                    : ! processor.hasKeyRecipe (selectedKey))
                return;
            const auto recipe = all ? processor.getPitchClassRecipe (selectedKey % 12)
                                    : processor.getKeyRecipe (selectedKey);
            setIntParameter (boilerplate::parameters::chordType, recipe.chordType);
            setIntParameter (boilerplate::parameters::complexity, recipe.complexity);
            setIntParameter (boilerplate::parameters::voicing, recipe.voicing);
            setIntParameter (boilerplate::parameters::pitchOffset, recipe.pitchOffset);
            updateKeyMapStatus();
        }

        void clearSelectedKeyRecipe()
        {
            if (allOctavesButton.getToggleState())
                processor.clearPitchClassRecipe (selectedKey % 12);
            else
                processor.clearKeyRecipe (selectedKey);
            updateKeyMapStatus();
        }

        void updateKeyMapStatus()
        {
            const auto all = allOctavesButton.getToggleState();
            const auto hasOverride = all ? processor.hasPitchClassRecipe (selectedKey % 12)
                                         : processor.hasKeyRecipe (selectedKey);
            const auto recipe = all ? processor.getPitchClassRecipe (selectedKey % 12)
                                    : processor.getKeyRecipe (selectedKey);

            juce::String status;

            if (hasOverride)
            {
                const auto pitch = recipe.pitchOffset > 0
                    ? "+" + juce::String (recipe.pitchOffset)
                    : juce::String (recipe.pitchOffset);

                status << (all ? "CLASS  " : "NOTE  ")
                       << choiceLabel (boilerplate::parameters::chordType, recipe.chordType)
                       << "/"
                       << choiceLabel (boilerplate::parameters::complexity, recipe.complexity)
                       << "/"
                       << choiceLabel (boilerplate::parameters::voicing, recipe.voicing)
                       << "/" << pitch;

                keyStatusLabel.setColour (juce::Label::textColourId, miami::brightMint);
            }
            else
            {
                status = ! all && processor.hasPitchClassRecipe (selectedKey % 12)
                    ? "CLASS  (inherited)" : "GLOBAL  (uses matrix)";
                keyStatusLabel.setColour (juce::Label::textColourId,
                                          miami::highlighterYellow.withAlpha (0.85f));
            }

            if (all && processor.hasKeyRecipe (selectedKey))
                status << "  [NOTE overrides]";
            if (keyStatusLabel.getText() != status)
                keyStatusLabel.setText (status, juce::dontSendNotification);

            if (loadKeyButton.isEnabled() != hasOverride)
                loadKeyButton.setEnabled (hasOverride);

            if (clearKeyButton.isEnabled() != hasOverride)
                clearKeyButton.setEnabled (hasOverride);
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

        void timerCallback() override
        {
            updateKeyMapStatus();
        }

        PluginProcessor& processor;
        juce::AudioProcessorValueTreeState& state;

        juce::Label keyMapTitle;
        juce::ComboBox keySelector;
        juce::Label keyStatusLabel;
        juce::TextButton loadKeyButton;
        juce::TextButton assignKeyButton;
        juce::TextButton clearKeyButton;
        juce::ToggleButton allOctavesButton;

        int selectedKey = 60;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyMapBar)
    };
}
