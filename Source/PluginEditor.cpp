#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "State/ParameterIDs.h"
#include "UI/OxaniumFonts.h"

namespace
{
    constexpr int designWidth = 720;
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
}

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& owner)
    : AudioProcessorEditor (&owner),
      processorRef (owner),
      chordPanel (processorRef),
      gainAttachment (processorRef.getValueTreeState(), boilerplate::parameters::gain, gainSlider),
      bypassAttachment (processorRef.getValueTreeState(), boilerplate::parameters::bypass, bypassButton)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    gainSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 92, 22);
    gainSlider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                    juce::MathConstants<float>::pi * 2.8f,
                                    true);
    gainSlider.setDoubleClickReturnValue (true, 0.0);
    gainSlider.setColour (juce::Slider::textBoxTextColourId,
                          wicky::ui::miami::brightMint);
    gainSlider.setColour (juce::Slider::textBoxBackgroundColourId,
                          wicky::ui::miami::midnightArcade);
    gainSlider.setColour (juce::Slider::textBoxOutlineColourId,
                          wicky::ui::miami::radicalBlue.withAlpha (0.55f));
    gainSlider.setName ("MIDI Velocity Gain");
    gainSlider.setDescription ("Scales the velocity of outgoing MIDI notes in decibels");
    addAndMakeVisible (gainSlider);

    bypassButton.setButtonText ("BYPASS");
    bypassButton.setName ("Bypass");
    bypassButton.setDescription ("Passes MIDI through without velocity scaling");
    bypassButton.setColour (juce::ToggleButton::textColourId,
                            wicky::ui::miami::textPrimary);
    bypassButton.setColour (juce::ToggleButton::tickColourId,
                            wicky::ui::miami::brightMint);
    addAndMakeVisible (bypassButton);

    addAndMakeVisible (chordPanel);

    setResizable (true, true);
    setResizeLimits (560, 450, 1400, 1130);

    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) designWidth / (double) designHeight);

    setSize (designWidth, designHeight);
}

PluginEditor::~PluginEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Deep matte dark background, with a barely-there electric-blue bloom
    // behind the title.
    g.setColour (wicky::ui::miami::midnightArcade);
    g.fillRect (bounds);

    g.setGradientFill (juce::ColourGradient (
        wicky::ui::miami::radicalBlue.withAlpha (0.10f), bounds.getCentreX(), bounds.getY(),
        juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getHeight() * 0.42f,
        false));
    g.fillRect (bounds);

    g.setColour (wicky::ui::miami::radicalBlue.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (wicky::ui::miami::fluorescentPink.withAlpha (0.18f));
    g.drawRoundedRectangle (bounds.reduced (3.5f), 6.0f, 1.0f);

    const auto bracketInset = 8.0f;
    drawCornerBracket (g, { bracketInset, bracketInset }, 1.0f, 1.0f);
    drawCornerBracket (g, { bounds.getRight() - bracketInset, bracketInset }, -1.0f, 1.0f);
    drawCornerBracket (g, { bracketInset, bounds.getBottom() - bracketInset }, 1.0f, -1.0f);
    drawCornerBracket (g, { bounds.getRight() - bracketInset, bounds.getBottom() - bracketInset },
                       -1.0f, -1.0f);

    // -------------------------------------------------------------------------
    // Header
    // -------------------------------------------------------------------------
    if (! headerArea.isEmpty())
    {
        auto header = headerArea.toFloat();
        const auto headerHeight = header.getHeight();

        auto titleArea = header.removeFromTop (headerHeight * 0.62f);
        auto subtitleArea = header.removeFromTop (headerHeight * 0.23f);

        const auto titleFont = boilerplate::ui::fonts::oxanium (
            juce::jlimit (18.0f, 26.0f, static_cast<float> (getHeight()) * 0.043f), 700.0f)
                                   .withExtraKerningFactor (0.07f);

        g.setFont (titleFont);
        g.setColour (wicky::ui::miami::fluorescentPink.withAlpha (0.35f));
        g.drawText (JucePlugin_Name, titleArea.translated (0.0f, 1.5f),
                    juce::Justification::centred, false);

        g.setColour (wicky::ui::miami::textPrimary);
        g.drawText (JucePlugin_Name, titleArea, juce::Justification::centred, false);

        const auto subtitleFont = boilerplate::ui::fonts::oxanium (
            juce::jmax (8.5f, static_cast<float> (getHeight()) * 0.018f), 700.0f)
                                      .withExtraKerningFactor (0.22f);

        g.setFont (subtitleFont);
        g.setColour (wicky::ui::miami::highlighterYellow.withAlpha (0.88f));
        g.drawText ("CHORD MATRIX  //  MIDI VELOCITY", subtitleArea,
                    juce::Justification::centred, false);

        juce::ColourGradient underline (
            wicky::ui::miami::radicalBlue, static_cast<float> (headerArea.getX()), 0.0f,
            wicky::ui::miami::fluorescentPink, static_cast<float> (headerArea.getRight()), 0.0f,
            false);
        underline.addColour (0.72, wicky::ui::miami::brightMint);

        g.setGradientFill (underline);
        g.fillRect (headerArea.getX(), headerArea.getBottom() - 2,
                    headerArea.getWidth(), 2);
    }

    // -------------------------------------------------------------------------
    // Output strip
    // -------------------------------------------------------------------------
    if (! controlsArea.isEmpty())
    {
        const auto card = controlsArea.toFloat();

        g.setColour (wicky::ui::miami::controlFace.withAlpha (0.92f));
        g.fillRoundedRectangle (card, 8.0f);

        g.setColour (wicky::ui::miami::radicalBlue.withAlpha (0.80f));
        g.drawRoundedRectangle (card.reduced (0.5f), 8.0f, 1.2f);

        g.setColour (wicky::ui::miami::radicalBlue.withAlpha (0.35f));
        g.fillRect (controlsArea.getCentreX(),
                    controlsArea.getY() + 12,
                    1, juce::jmax (0, controlsArea.getHeight() - 24));

        g.setColour (wicky::ui::miami::brightMint.withAlpha (0.85f));
        g.fillEllipse (static_cast<float> (controlsArea.getRight() - 22),
                       static_cast<float> (controlsArea.getY() + 14), 5.0f, 5.0f);
    }

    // -------------------------------------------------------------------------
    // Footer
    // -------------------------------------------------------------------------
    if (! footerArea.isEmpty())
    {
        const auto footerFont = boilerplate::ui::fonts::oxanium (
            juce::jmax (8.0f, static_cast<float> (getHeight()) * 0.015f), 400.0f)
                                    .withExtraKerningFactor (0.12f);

        g.setFont (footerFont);
        g.setColour (wicky::ui::miami::fluorescentPink.withAlpha (0.70f));
        g.drawText ("MIAMI NIGHTS", footerArea, juce::Justification::centred, false);
    }
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced (scaled (16));

    headerArea = area.removeFromTop (scaled (58));
    area.removeFromTop (scaled (8));

    footerArea = area.removeFromBottom (scaled (18));
    area.removeFromBottom (scaled (2));

    const auto controlsHeight = juce::jlimit (scaled (78), scaled (92),
                                              area.getHeight() / 5);

    controlsArea = area.removeFromBottom (controlsHeight);
    area.removeFromBottom (scaled (10));

    chordPanel.setBounds (area);

    auto knobArea = controlsArea.reduced (scaled (14), scaled (6))
                               .removeFromLeft (controlsArea.getWidth() / 2);

    gainSlider.setBounds (knobArea.withSizeKeepingCentre (
        juce::jmin (knobArea.getWidth(), scaled (170)),
        juce::jmin (knobArea.getHeight(), scaled (80))));

    auto buttonArea = controlsArea.reduced (scaled (14), scaled (6))
                                 .withTrimmedLeft (controlsArea.getWidth() / 2);

    bypassButton.setBounds (buttonArea.withSizeKeepingCentre (
        juce::jmin (buttonArea.getWidth(), scaled (168)),
        juce::jmin (buttonArea.getHeight(), scaled (36))));
}

int PluginEditor::scaled (int value) const noexcept
{
    const auto widthScale = (float) getWidth() / (float) designWidth;
    const auto heightScale = (float) getHeight() / (float) designHeight;

    return juce::roundToInt ((float) value * juce::jmin (widthScale, heightScale));
}
