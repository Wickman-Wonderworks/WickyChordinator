#include "UI/BoilerplateLookAndFeel.h"

namespace boilerplate::ui
{
    BoilerplateLookAndFeel::BoilerplateLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId, palette::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, palette::track);
        setColour (juce::Slider::textBoxTextColourId, palette::cream);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, palette::cream);
        setColour (juce::ToggleButton::textColourId, palette::cream);
        setColour (juce::ToggleButton::tickColourId, palette::accent);
        setColour (juce::TooltipWindow::backgroundColourId, palette::panelTop);
        setColour (juce::TooltipWindow::textColourId, palette::cream);
        setColour (juce::TooltipWindow::outlineColourId, palette::frame);
    }

    void BoilerplateLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                                   int x, int y, int width, int height,
                                                   float sliderPosProportional,
                                                   float rotaryStartAngle,
                                                   float rotaryEndAngle,
                                                   juce::Slider&)
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y,
                                                    (float) width, (float) height)
                                .reduced (6.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = rotaryStartAngle
                         + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        const auto trackWidth = juce::jmax (2.0f, radius * 0.055f);
        const auto arcRadius = radius * 0.80f;

        // Tick ring around the outside of the value arc.
        g.setColour (palette::tick);
        constexpr int tickCount = 11;

        for (int i = 0; i < tickCount; ++i)
        {
            const auto proportion = (float) i / (float) (tickCount - 1);
            const auto tickAngle = rotaryStartAngle
                                 + proportion * (rotaryEndAngle - rotaryStartAngle);
            const auto inner = centre.getPointOnCircumference (radius * 0.90f, tickAngle);
            const auto outer = centre.getPointOnCircumference (radius * 0.96f, tickAngle);

            g.drawLine (inner.x, inner.y, outer.x, outer.y, 1.0f);
        }

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (palette::track);
        g.strokePath (track, juce::PathStrokeType (trackWidth,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        if (angle > rotaryStartAngle + 0.001f)
        {
            juce::Path valueArc;
            valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                    rotaryStartAngle, angle, true);
            g.setColour (palette::accent);
            g.strokePath (valueArc, juce::PathStrokeType (trackWidth,
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
        }

        const auto knobRadius = radius * 0.62f;
        const auto knobBounds = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f)
                                    .withCentre (centre);

        g.setGradientFill (juce::ColourGradient (palette::knobLight,
                                                 centre.x, centre.y - knobRadius,
                                                 palette::knobDark,
                                                 centre.x, centre.y + knobRadius,
                                                 false));
        g.fillEllipse (knobBounds);

        g.setColour (palette::knobEdge);
        g.drawEllipse (knobBounds, 1.2f);

        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawEllipse (knobBounds.reduced (3.0f), 1.0f);

        const auto pointerStart = centre.getPointOnCircumference (knobRadius * 0.34f, angle);
        const auto pointerEnd = centre.getPointOnCircumference (knobRadius * 0.82f, angle);

        g.setColour (palette::cream);
        g.drawLine (pointerStart.x, pointerStart.y, pointerEnd.x, pointerEnd.y,
                    juce::jmax (2.0f, trackWidth * 0.9f));
    }

    void BoilerplateLookAndFeel::drawToggleButton (juce::Graphics& g,
                                                   juce::ToggleButton& button,
                                                   bool shouldDrawButtonAsHighlighted,
                                                   bool shouldDrawButtonAsDown)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const auto isOn = button.getToggleState();

        auto fill = isOn ? palette::accentDark : palette::control;

        if (shouldDrawButtonAsDown)
            fill = fill.brighter (0.10f);
        else if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter (0.04f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (isOn ? palette::accent : palette::controlEdge);
        g.drawRoundedRectangle (bounds, 6.0f, 1.2f);

        auto led = bounds.removeFromLeft (bounds.getHeight())
                        .reduced (bounds.getHeight() * 0.32f);

        if (isOn)
        {
            g.setColour (palette::accent.withAlpha (0.22f));
            g.fillEllipse (led.expanded (3.0f));
        }

        g.setColour (isOn ? palette::accent : palette::ledOff);
        g.fillEllipse (led);

        g.setColour (isOn ? palette::panelTop : palette::cream);
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (11.0f, bounds.getHeight() * 0.38f),
                                                  juce::Font::bold)));
        g.drawText (button.getButtonText(), bounds.reduced (4.0f, 0.0f),
                    juce::Justification::centred, false);
    }

    juce::Font BoilerplateLookAndFeel::getLabelFont (juce::Label&)
    {
        return juce::Font (juce::FontOptions (13.0f, juce::Font::bold));
    }
}
