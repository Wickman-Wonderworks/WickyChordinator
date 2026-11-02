#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "State/ParameterIDs.h"
#include "UI/OxaniumFonts.h"

namespace
{
    constexpr int designWidth = 1000;
    constexpr int designHeight = 580;

    void drawCornerBracket (juce::Graphics& g, juce::Point<float> corner,
                            float xDirection, float yDirection)
    {
        constexpr float length = 15.0f;

        g.setColour (wicky::ui::miami::radicalBlue.withAlpha (0.90f));
        g.drawLine (corner.x, corner.y,
                    corner.x + xDirection * length, corner.y, 1.5f);

        g.setColour (wicky::ui::miami::fluorescentPink.withAlpha (0.90f));
        g.drawLine (corner.x, corner.y,
                    corner.x, corner.y + yDirection * length, 1.5f);
    }

    void styleFlatButton (juce::Button& button, const juce::String& text)
    {
        button.setButtonText (text);
        button.setColour (juce::TextButton::buttonColourId,
                          wicky::ui::miami::controlFace);
        button.setColour (juce::TextButton::textColourOffId,
                          wicky::ui::miami::highlighterYellow);
        button.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
}

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& owner)
    : AudioProcessorEditor (&owner),
      processorRef (owner),
      chordPanel (owner),
      globalPanel (owner),
      keyMapBar (owner),
      chordKeyboard (owner.getKeyboardState()),
      gainAttachment (owner.getValueTreeState(), boilerplate::parameters::gain, gainSlider),
      powerAttachment (owner.getValueTreeState(), boilerplate::parameters::power, powerButton)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    presetManager = std::make_unique<wicky::ui::PresetManager> (owner.getValueTreeState());

    configureStatusBar();

    addAndMakeVisible (chordPanel);
    addAndMakeVisible (globalPanel);
    addAndMakeVisible (keyMapBar);
    addAndMakeVisible (chordKeyboard);

    keyMapBar.onSelectedKeyChanged = [this] (int note)
    {
        chordKeyboard.setSelectedNote (note);
    };

    chordKeyboard.onNoteSelected = [this] (int note)
    {
        keyMapBar.setSelectedKey (note);
        chordKeyboard.setSelectedNote (note);
    };

    // UI mouse clicks are raw MIDI, exactly like a hardware controller: they
    // enter the processor's lock-free bridge and are expanded by the same chord
    // engine. Nothing is written straight into the display state.
    chordKeyboard.onRawNote = [this] (bool noteOn, int note, float velocity)
    {
        processorRef.queueKeyboardNote (noteOn, note, velocity);
    };

    chordKeyboard.setSelectedNote (keyMapBar.getSelectedKey());

    // The keyboard state mirrors processed output only. Clear anything left
    // behind by a previous editor before the first output sync runs.
    processorRef.getKeyboardState().allNotesOff (1);
    displayedNoteLow = 0;
    displayedNoteHigh = 0;

    setResizable (true, true);
    setResizeLimits (760, 441, 2000, 1160);

    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) designWidth / (double) designHeight);

    setSize (designWidth, designHeight);
    startTimerHz (24);
}

PluginEditor::~PluginEditor()
{
    stopTimer();

    // If the editor is closed mid-click, release the raw note so the engine
    // cannot leave a chord hanging.
    chordKeyboard.releaseHeldRawNote();

    // Clear the display mirror; any notes that are still held by the engine
    // will be mirrored again if a new editor is opened.
    processorRef.getKeyboardState().allNotesOff (1);

    setLookAndFeel (nullptr);
}

//==============================================================================
void PluginEditor::configureStatusBar()
{
    using namespace wicky::ui;

    presetCaption.setText ("PRESET", juce::dontSendNotification);
    presetCaption.setFont (boilerplate::ui::fonts::oxanium (9.5f, 700.0f)
                               .withExtraKerningFactor (0.16f));
    presetCaption.setColour (juce::Label::textColourId, miami::textMuted);
    presetCaption.setJustificationType (juce::Justification::centredLeft);
    presetCaption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (presetCaption);

    presetSelector.setColour (juce::ComboBox::backgroundColourId, miami::controlFace);
    presetSelector.setColour (juce::ComboBox::textColourId, miami::highlighterYellow);
    presetSelector.setColour (juce::ComboBox::outlineColourId,
                              miami::radicalBlue.withAlpha (0.70f));
    presetSelector.setColour (juce::ComboBox::arrowColourId, miami::brightMint);
    presetSelector.setColour (juce::ComboBox::focusedOutlineColourId, miami::fluorescentPink);
    presetSelector.setName ("Preset");
    presetSelector.setDescription ("Select a built-in preset");

    jassert (presetManager != nullptr);

    if (presetManager != nullptr)
    {
        for (int i = 0; i < presetManager->getNumPresets(); ++i)
            presetSelector.addItem (presetManager->getName (i), i + 1);

        presetSelector.setSelectedId (presetManager->getCurrentIndex() + 1,
                                      juce::dontSendNotification);
    }

    presetSelector.onChange = [this]
    {
        if (updatingPresetSelector)
            return;

        selectPreset (presetSelector.getSelectedId() - 1);
    };
    addAndMakeVisible (presetSelector);

    styleFlatButton (presetPreviousButton, "<");
    styleFlatButton (presetNextButton, ">");
    presetPreviousButton.setName ("Previous preset");
    presetNextButton.setName ("Next preset");

    presetPreviousButton.onClick = [this]
    {
        if (presetManager == nullptr || presetManager->getNumPresets() == 0)
            return;

        selectPreset ((presetManager->getCurrentIndex()
                       + presetManager->getNumPresets() - 1)
                      % presetManager->getNumPresets());
    };

    presetNextButton.onClick = [this]
    {
        if (presetManager == nullptr || presetManager->getNumPresets() == 0)
            return;

        selectPreset ((presetManager->getCurrentIndex() + 1)
                      % presetManager->getNumPresets());
    };

    addAndMakeVisible (presetPreviousButton);
    addAndMakeVisible (presetNextButton);

    chordCaption.setText ("NOW PLAYING", juce::dontSendNotification);
    chordCaption.setFont (boilerplate::ui::fonts::oxanium (9.5f, 700.0f)
                              .withExtraKerningFactor (0.24f));
    chordCaption.setColour (juce::Label::textColourId,
                            miami::highlighterYellow.withAlpha (0.82f));
    chordCaption.setJustificationType (juce::Justification::centred);
    chordCaption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (chordCaption);

    chordReadout.setText ("--", juce::dontSendNotification);
    chordReadout.setFont (boilerplate::ui::fonts::oxanium (30.0f, 700.0f)
                              .withExtraKerningFactor (0.04f));
    chordReadout.setMinimumHorizontalScale (0.55f);
    chordReadout.setColour (juce::Label::textColourId, miami::textMuted);
    chordReadout.setJustificationType (juce::Justification::centred);
    chordReadout.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (chordReadout);

    powerButton.setButtonText ("POWER");
    powerButton.getProperties().set ("slidingSwitch", true);
    powerButton.setName ("Power");
    powerButton.setDescription ("Globally enable or disable chord generation");
    powerButton.setColour (juce::ToggleButton::textColourId, miami::textPrimary);
    powerButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (powerButton);

    gainSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    gainSlider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                    juce::MathConstants<float>::pi * 2.8f,
                                    true);
    gainSlider.setDoubleClickReturnValue (true, 0.0);
    gainSlider.setColour (juce::Slider::textBoxTextColourId, miami::brightMint);
    gainSlider.setColour (juce::Slider::textBoxBackgroundColourId, miami::midnightArcade);
    gainSlider.setColour (juce::Slider::textBoxOutlineColourId,
                          miami::radicalBlue.withAlpha (0.55f));
    gainSlider.getProperties().set ("dotRing", true);
    gainSlider.setName ("Velocity");
    gainSlider.setDescription ("Scales outgoing MIDI velocity in decibels");
    addAndMakeVisible (gainSlider);
}

void PluginEditor::selectPreset (int index)
{
    if (presetManager == nullptr || ! presetManager->loadPreset (index))
        return;

    updatingPresetSelector = true;
    presetSelector.setSelectedId (presetManager->getCurrentIndex() + 1,
                                  juce::dontSendNotification);
    updatingPresetSelector = false;

    refreshChordReadout();
}

//==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    using namespace wicky::ui;

    const auto bounds = getLocalBounds().toFloat();

    // Deep matte dark background, with a barely-there electric-blue bloom.
    g.setColour (miami::midnightArcade);
    g.fillRect (bounds);

    g.setGradientFill (juce::ColourGradient (
        miami::radicalBlue.withAlpha (0.10f), bounds.getCentreX(), bounds.getY(),
        juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getHeight() * 0.42f,
        false));
    g.fillRect (bounds);

    g.setColour (miami::radicalBlue.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (miami::fluorescentPink.withAlpha (0.18f));
    g.drawRoundedRectangle (bounds.reduced (3.5f), 6.0f, 1.0f);

    const auto bracketInset = 8.0f;
    drawCornerBracket (g, { bracketInset, bracketInset }, 1.0f, 1.0f);
    drawCornerBracket (g, { bounds.getRight() - bracketInset, bracketInset }, -1.0f, 1.0f);
    drawCornerBracket (g, { bracketInset, bounds.getBottom() - bracketInset }, 1.0f, -1.0f);
    drawCornerBracket (g, { bounds.getRight() - bracketInset, bounds.getBottom() - bracketInset },
                       -1.0f, -1.0f);

    // Top status bar card.
    if (! statusArea.isEmpty())
    {
        auto status = statusArea.toFloat();

        g.setColour (miami::controlFace.withAlpha (0.94f));
        g.fillRoundedRectangle (status, 8.0f);

        g.setColour (miami::radicalBlue.withAlpha (0.90f));
        g.drawRoundedRectangle (status.reduced (0.5f), 8.0f, 1.4f);
    }

    // The large chord readout sits in its own neon pill.
    if (! chordReadout.getBounds().isEmpty())
    {
        auto pill = chordReadout.getBounds().toFloat().expanded (10.0f, 4.0f);

        g.setColour (miami::midnightArcade.withAlpha (0.92f));
        g.fillRoundedRectangle (pill, 8.0f);

        g.setColour (lastSoundState
                         ? miami::brightMint.withAlpha (0.90f)
                         : miami::fluorescentPink.withAlpha (0.48f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 8.0f, 1.3f);

        g.setColour (miami::brightMint.withAlpha (0.20f));
        g.fillEllipse (pill.getRight() - 16.0f, pill.getY() + 8.0f, 4.0f, 4.0f);
    }

    // Keyboard frame.
    if (! keyboardArea.isEmpty())
    {
        g.setColour (miami::fluorescentPink.withAlpha (0.90f));
        g.drawRoundedRectangle (keyboardArea.toFloat().expanded (1.0f), 4.0f, 1.5f);

        g.setColour (miami::radicalBlue.withAlpha (0.45f));
        g.fillRect (keyboardArea.getX(), keyboardArea.getY() - 4,
                    keyboardArea.getWidth(), 1);
    }

    if (! footerArea.isEmpty())
    {
        const auto footerFont = boilerplate::ui::fonts::oxanium (
            juce::jmax (8.0f, static_cast<float> (getHeight()) * 0.015f), 400.0f)
                                    .withExtraKerningFactor (0.12f);

        g.setFont (footerFont);
        g.setColour (miami::fluorescentPink.withAlpha (0.70f));
        g.drawText ("WICKY CHORDINATOR  //  MIAMI NIGHTS",
                    footerArea, juce::Justification::centred, false);
    }
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced (scaled (14));

    statusArea = area.removeFromTop (scaled (74));
    area.removeFromTop (scaled (8));

    footerArea = area.removeFromBottom (scaled (16));
    area.removeFromBottom (scaled (4));

    keyboardArea = area.removeFromBottom (scaled (66));
    area.removeFromBottom (scaled (6));

    keyMapArea = area.removeFromBottom (scaled (30));
    area.removeFromBottom (scaled (8));

    leftColumnArea = area.removeFromLeft (
        juce::roundToInt (static_cast<float> (area.getWidth()) * 0.56f));
    area.removeFromLeft (scaled (8));
    rightColumnArea = area;

    chordPanel.setBounds (leftColumnArea);
    globalPanel.setBounds (rightColumnArea);
    keyMapBar.setBounds (keyMapArea);
    chordKeyboard.setBounds (keyboardArea);

    if (keyboardArea.getWidth() > 0)
        chordKeyboard.setKeyWidth (juce::jmax (12.0f,
            static_cast<float> (keyboardArea.getWidth()) / 22.0f));

    layoutStatusBar (statusArea);
}

void PluginEditor::layoutStatusBar (juce::Rectangle<int> area)
{
    auto bar = area.reduced (scaled (6), scaled (5));

    auto left = bar.removeFromLeft (scaled (370));

    presetCaption.setBounds (left.removeFromLeft (scaled (54))
                                 .reduced (0, scaled (11)));
    presetSelector.setBounds (left.removeFromLeft (scaled (226))
                                  .reduced (0, scaled (11)));
    left.removeFromLeft (scaled (4));
    presetPreviousButton.setBounds (left.removeFromLeft (scaled (32))
                                        .reduced (0, scaled (11)));
    left.removeFromLeft (scaled (4));
    presetNextButton.setBounds (left.removeFromLeft (scaled (32))
                                    .reduced (0, scaled (11)));

    auto right = bar.removeFromRight (scaled (232));

    powerButton.setBounds (right.removeFromRight (scaled (128))
                               .reduced (0, scaled (9)));
    right.removeFromRight (scaled (6));
    gainSlider.setBounds (right.removeFromRight (scaled (90))
                              .reduced (0, scaled (2)));

    auto centre = bar;
    chordCaption.setBounds (centre.removeFromTop (scaled (15)));
    centre.removeFromTop (scaled (3)); // breathing room under the caption
    chordReadout.setBounds (centre.withTrimmedBottom (scaled (10)));
}

int PluginEditor::scaled (int value) const noexcept
{
    const auto widthScale = static_cast<float> (getWidth()) / static_cast<float> (designWidth);
    const auto heightScale = static_cast<float> (getHeight()) / static_cast<float> (designHeight);

    return juce::roundToInt (static_cast<float> (value)
                             * juce::jmin (widthScale, heightScale));
}

//==============================================================================
void PluginEditor::timerCallback()
{
    refreshChordReadout();
    syncKeyboardDisplay();
}

void PluginEditor::syncKeyboardDisplay()
{
    const auto outputLow = processorRef.getActiveOutputNotes (0);
    const auto outputHigh = processorRef.getActiveOutputNotes (1);

    if (outputLow == displayedNoteLow && outputHigh == displayedNoteHigh)
        return;

    auto& keyboardState = processorRef.getKeyboardState();

    for (int note = 0; note < 128; ++note)
    {
        const auto bit = static_cast<juce::uint64> (1) << (note & 63);
        const auto outputWord = note < 64 ? outputLow : outputHigh;
        const auto displayedWord = note < 64 ? displayedNoteLow : displayedNoteHigh;
        const auto isOn = (outputWord & bit) != 0;
        const auto wasOn = (displayedWord & bit) != 0;

        if (isOn == wasOn)
            continue;

        if (isOn)
            keyboardState.noteOn (1, note, 1.0f);
        else
            keyboardState.noteOff (1, note, 0.0f);
    }

    displayedNoteLow = outputLow;
    displayedNoteHigh = outputHigh;
}

void PluginEditor::refreshChordReadout()
{
    const auto symbol = currentChordSymbol();
    const auto sounding = processorRef.getNumSoundingChords() > 0;

    if (chordReadout.getText() != symbol)
        chordReadout.setText (symbol, juce::dontSendNotification);

    if (sounding != lastSoundState)
    {
        lastSoundState = sounding;
        chordReadout.setColour (juce::Label::textColourId,
                                sounding ? wicky::ui::miami::brightMint
                                         : wicky::ui::miami::textMuted);
        repaint (statusArea);
    }
}

juce::String PluginEditor::currentChordSymbol() const
{
    const auto rootNote = processorRef.getLastTriggeredChordNote();

    if (rootNote < 0 || processorRef.getNumSoundingChords() <= 0)
        return "--";

    return chordSymbolFor (rootNote, processorRef.getLastTriggeredChordRecipe());
}

juce::String PluginEditor::chordSymbolFor (int rootNote,
                                           const PluginProcessor::ChordRecipe& recipe)
{
    static const char* const noteNames[12] =
    {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    static const char* const majorSuffix[11] =
    {
        "maj", "6", "maj7", "add9", "maj9", "6/9",
        "maj11", "maj13", "7b5", "9#11", "13#11"
    };

    static const char* const minorSuffix[11] =
    {
        "m", "m6", "m7", "madd9", "m9", "m6/9",
        "m11", "m13", "m7b5", "m9#11", "m13#11"
    };

    static const char* const dominantSuffix[11] =
    {
        "", "6", "7", "add9", "9", "6/9",
        "11", "13", "7b5", "9#11", "13#11"
    };

    static const char* const otherSuffix[11] =
    {
        "dim", "dim6", "dim7", "dimadd9", "dim9", "dim6/9",
        "dim11", "dim13", "m7b5", "m9#11", "m13#11"
    };

    const auto type = juce::jlimit (0, 3, recipe.chordType);
    const auto complexity = juce::jlimit (0, 10, recipe.complexity);

    const char* suffix = "";

    switch (type)
    {
        case 0: suffix = majorSuffix[complexity]; break;
        case 1: suffix = minorSuffix[complexity]; break;
        case 2: suffix = dominantSuffix[complexity]; break;
        default: suffix = otherSuffix[complexity]; break;
    }

    const juce::String root (noteNames[juce::jlimit (0, 127, rootNote) % 12]);
    const juce::String suffixString (suffix);

    return suffixString.isEmpty() ? root : root + " " + suffixString;
}
