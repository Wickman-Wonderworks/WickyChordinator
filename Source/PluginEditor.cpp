#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "State/ParameterIDs.h"

namespace
{
    constexpr int designWidth = 720;
    constexpr int designHeight = 580;

    void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius)
    {
        const auto bounds = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                                .withCentre (centre);

        g.setColour (boilerplate::ui::palette::screw.withAlpha (0.35f));
        g.fillEllipse (bounds);
        g.setColour (boilerplate::ui::palette::screw);
        g.drawEllipse (bounds, 1.0f);

        juce::Path slot;
        slot.addRectangle (-radius * 0.62f, -radius * 0.13f,
                           radius * 1.24f, radius * 0.26f);
        slot.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f)
                                 .translated (centre.x, centre.y));

        g.setColour (boilerplate::ui::palette::screwSlot);
        g.fillPath (slot);
    }
}

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& owner)
    : AudioProcessorEditor (&owner),
      processorRef (owner),
      chordPanel (processorRef.getValueTreeState()),
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
    gainSlider.setName ("MIDI Velocity Gain");
    gainSlider.setDescription ("Scales the velocity of outgoing MIDI notes in decibels");
    addAndMakeVisible (gainSlider);

    bypassButton.setButtonText ("BYPASS");
    bypassButton.setName ("Bypass");
    bypassButton.setDescription ("Passes MIDI through without velocity scaling");
    addAndMakeVisible (bypassButton);

    addAndMakeVisible (chordPanel);

    setResizable (true, true);
    setResizeLimits (560, 450, 1200, 970);

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

    g.setGradientFill (juce::ColourGradient (boilerplate::ui::palette::panelTop,
                                             bounds.getCentreX(), bounds.getY(),
                                             boilerplate::ui::palette::panelBottom,
                                             bounds.getCentreX(), bounds.getBottom(),
                                             false));
    g.fillRect (bounds);

    g.setColour (boilerplate::ui::palette::frame);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawRoundedRectangle (bounds.reduced (4.5f), 6.0f, 1.0f);

    const auto screwInset = (float) scaled (14);
    const auto screwRadius = (float) scaled (5);

    drawScrew (g, { screwInset, screwInset }, screwRadius);
    drawScrew (g, { bounds.getRight() - screwInset, screwInset }, screwRadius);
    drawScrew (g, { screwInset, bounds.getBottom() - screwInset }, screwRadius);
    drawScrew (g, { bounds.getRight() - screwInset, bounds.getBottom() - screwInset }, screwRadius);

    auto area = getLocalBounds().reduced (scaled (20));
    auto header = area.removeFromTop (scaled (64));

    auto titleArea = header.removeFromTop (scaled (40));
    const auto titleFont = juce::Font (juce::FontOptions (
        juce::jmax (18.0f, (float) getHeight() * 0.080f), juce::Font::bold))
                               .withExtraKerningFactor (0.06f);

    g.setFont (titleFont);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawText (JucePlugin_Name, titleArea.translated (0, 1),
                juce::Justification::centred, false);
    g.setColour (boilerplate::ui::palette::cream);
    g.drawText (JucePlugin_Name, titleArea, juce::Justification::centred, false);

    const auto subtitleFont = juce::Font (juce::FontOptions (
        juce::jmax (8.5f, (float) getHeight() * 0.029f), juce::Font::plain))
                                  .withExtraKerningFactor (0.18f);

    g.setFont (subtitleFont);
    g.setColour (boilerplate::ui::palette::muted);
    g.drawText ("JUCE 9  /  CHORD MATRIX  /  MIDI VELOCITY", header,
                juce::Justification::centred, false);

    const auto divider = area.removeFromTop (scaled (1));
    g.setColour (boilerplate::ui::palette::line);
    g.fillRect (divider);

    const auto footer = getLocalBounds().reduced (scaled (20)).removeFromBottom (scaled (24));
    const auto footerFont = juce::Font (juce::FontOptions (
        juce::jmax (8.0f, (float) getHeight() * 0.026f), juce::Font::plain))
                                .withExtraKerningFactor (0.12f);

    g.setFont (footerFont);
    g.setColour (boilerplate::ui::palette::muted);

    const char* formats = "VST3 / STANDALONE";
   #if JUCE_MAC
    formats = "VST3 / AU / STANDALONE";
   #endif

    g.drawText (formats, footer, juce::Justification::centredLeft, false);
    g.drawText ("v" JucePlugin_VersionString, footer, juce::Justification::centredRight, false);
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced (scaled (20));

    area.removeFromTop (scaled (64));   // title block drawn in paint()
    area.removeFromBottom (scaled (24)); // footer drawn in paint()
    area.removeFromBottom (scaled (8));

    const auto controlsHeight = juce::jmin (area.getHeight() / 3, scaled (132));
    auto controls = area.removeFromBottom (controlsHeight);
    area.removeFromBottom (scaled (10));

    chordPanel.setBounds (area);

    auto knobArea = controls.removeFromLeft (controls.getWidth() / 2);
    const auto knobSize = juce::jmin (knobArea.getWidth(), knobArea.getHeight());
    gainSlider.setBounds (knobArea.withSizeKeepingCentre (knobSize, knobSize));

    bypassButton.setBounds (controls.withSizeKeepingCentre (scaled (150), scaled (36)));
}

int PluginEditor::scaled (int value) const noexcept
{
    const auto widthScale = (float) getWidth() / (float) designWidth;
    const auto heightScale = (float) getHeight() / (float) designHeight;

    return juce::roundToInt ((float) value * juce::jmin (widthScale, heightScale));
}
