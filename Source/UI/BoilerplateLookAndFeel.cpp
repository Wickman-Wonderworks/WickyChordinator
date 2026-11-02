#include "UI/BoilerplateLookAndFeel.h"
#include "UI/OxaniumFonts.h"

namespace boilerplate::ui
{
    BoilerplateLookAndFeel::BoilerplateLookAndFeel()
    {
        fonts::initialiseOxanium();
        setDefaultSansSerifTypeface (fonts::oxaniumTypeface (400.0f));

        setColour (juce::Slider::rotarySliderFillColourId, palette::fuchsia);
        setColour (juce::Slider::rotarySliderOutlineColourId, palette::track);
        setColour (juce::Slider::textBoxTextColourId, palette::toxicGreen);
        setColour (juce::Slider::textBoxBackgroundColourId, palette::midnight);
        setColour (juce::Slider::textBoxOutlineColourId, palette::electricBlue.withAlpha (0.55f));

        setColour (juce::Label::textColourId, palette::cream);

        setColour (juce::TextButton::buttonColourId, palette::control);
        setColour (juce::TextButton::buttonOnColourId, palette::electricBlue);
        setColour (juce::TextButton::textColourOffId, palette::fuchsia);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);

        setColour (juce::ToggleButton::textColourId, palette::cream);
        setColour (juce::ToggleButton::tickColourId, palette::toxicGreen);

        setColour (juce::ComboBox::backgroundColourId, palette::control);
        setColour (juce::ComboBox::textColourId, palette::fuchsia);
        setColour (juce::ComboBox::outlineColourId, palette::electricBlue.withAlpha (0.65f));
        setColour (juce::ComboBox::arrowColourId, palette::cyberYellow);
        setColour (juce::ComboBox::focusedOutlineColourId, palette::toxicGreen);

        setColour (juce::PopupMenu::backgroundColourId, palette::midnight);
        setColour (juce::PopupMenu::textColourId, palette::cream);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, palette::electricBlue);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

        setColour (juce::TooltipWindow::backgroundColourId, palette::midnight);
        setColour (juce::TooltipWindow::textColourId, palette::cream);
        setColour (juce::TooltipWindow::outlineColourId, palette::fuchsia);
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

            g.setColour (proportion <= sliderPosProportional
                             ? palette::cyberYellow.withAlpha (0.90f)
                             : palette::tick.withAlpha (0.55f));
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

            g.setColour (palette::fuchsia.withAlpha (0.22f));
            g.strokePath (valueArc, juce::PathStrokeType (trackWidth * 2.6f,
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));

            g.setColour (palette::fuchsia);
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

        g.setColour (palette::fuchsia.withAlpha (0.18f));
        g.drawEllipse (knobBounds.reduced (3.0f), 1.0f);

        const auto pointerStart = centre.getPointOnCircumference (knobRadius * 0.34f, angle);
        const auto pointerEnd = centre.getPointOnCircumference (knobRadius * 0.82f, angle);

        g.setColour (palette::cyberYellow);
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

        auto fill = isOn ? palette::electricBlue : palette::control;

        if (shouldDrawButtonAsDown)
            fill = fill.brighter (0.10f);
        else if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter (0.05f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (isOn ? palette::toxicGreen : palette::electricBlue.withAlpha (0.75f));
        g.drawRoundedRectangle (bounds, 6.0f, isOn ? 1.6f : 1.2f);

        auto led = bounds.removeFromLeft (bounds.getHeight())
                        .reduced (bounds.getHeight() * 0.32f);

        if (isOn)
        {
            g.setColour (palette::toxicGreen.withAlpha (0.25f));
            g.fillEllipse (led.expanded (3.0f));
        }

        g.setColour (isOn ? palette::toxicGreen : palette::ledOff);
        g.fillEllipse (led);

        g.setColour (isOn ? juce::Colours::white : palette::cream);
        g.setFont (fonts::oxanium (juce::jmax (11.0f, bounds.getHeight() * 0.38f), 700.0f)
                       .withExtraKerningFactor (0.10f));
        g.drawText (button.getButtonText(), bounds.reduced (4.0f, 0.0f),
                    juce::Justification::centred, false);
    }

    void BoilerplateLookAndFeel::drawButtonBackground (juce::Graphics& g,
                                                       juce::Button& button,
                                                       const juce::Colour&,
                                                       bool shouldDrawButtonAsHighlighted,
                                                       bool shouldDrawButtonAsDown)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const auto radius = juce::jmin (6.0f, bounds.getHeight() * 0.28f);
        const auto isOn = button.getToggleState();

        auto fill = button.findColour (juce::TextButton::buttonColourId);

        if (isOn)
            fill = button.findColour (juce::TextButton::buttonOnColourId);

        if (shouldDrawButtonAsDown)
            fill = fill.brighter (0.16f);
        else if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter (0.07f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, radius);

        g.setColour (isOn ? palette::toxicGreen
                          : palette::electricBlue.withAlpha (0.70f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, isOn ? 1.6f : 1.0f);

        if (isOn)
        {
            g.setColour (palette::toxicGreen.withAlpha (0.14f));
            g.fillRoundedRectangle (bounds.reduced (2.5f), juce::jmax (2.0f, radius * 0.7f));
        }
    }

    juce::Font BoilerplateLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return fonts::oxanium (
            juce::jlimit (10.0f, 14.0f, static_cast<float> (buttonHeight) * 0.48f), 700.0f)
            .withExtraKerningFactor (0.04f);
    }

    juce::Font BoilerplateLookAndFeel::getLabelFont (juce::Label&)
    {
        return fonts::oxanium (12.0f, 700.0f)
            .withExtraKerningFactor (0.06f);
    }

    juce::Typeface::Ptr BoilerplateLookAndFeel::getTypefaceForFont (const juce::Font& font)
    {
        if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            return fonts::oxaniumTypeface (font.isBold() ? 700.0f : 400.0f);

        return juce::LookAndFeel_V4::getTypefaceForFont (font);
    }
}
