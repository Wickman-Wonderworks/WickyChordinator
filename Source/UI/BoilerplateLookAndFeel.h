#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace boilerplate::ui
{
    namespace palette
    {
        // Miami Nights core palette.
        inline const juce::Colour midnight       { 0xff121214 };
        inline const juce::Colour electricBlue   { 0xff0055ff };
        inline const juce::Colour fuchsia        { 0xffff1493 };
        inline const juce::Colour cyberYellow    { 0xffeeee00 };
        inline const juce::Colour toxicGreen     { 0xff39ff14 };

        // Derived surfaces.
        inline const juce::Colour panelTop       { 0xff121214 };
        inline const juce::Colour panelBottom    { 0xff121214 };
        inline const juce::Colour frame          { 0xff0055ff };
        inline const juce::Colour line           { 0xff2a2a34 };
        inline const juce::Colour cream          { 0xfff2f2f7 };
        inline const juce::Colour muted          { 0xff8a8a99 };
        inline const juce::Colour accent         { 0xffff1493 };
        inline const juce::Colour accentDark     { 0xff0055ff };
        inline const juce::Colour track          { 0xff2e2e39 };
        inline const juce::Colour tick           { 0xff0055ff };
        inline const juce::Colour knobLight      { 0xff2b2b35 };
        inline const juce::Colour knobDark       { 0xff121214 };
        inline const juce::Colour knobEdge       { 0xff0055ff };
        inline const juce::Colour control        { 0xff1a1a21 };
        inline const juce::Colour controlEdge    { 0xff0055ff };
        inline const juce::Colour ledOff         { 0xff3a3a46 };
        inline const juce::Colour screw          { 0xff2e2e39 };
        inline const juce::Colour screwSlot      { 0xff121214 };
    }

    /** Neon hardware look for the Chordinator editor. */
    class BoilerplateLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        BoilerplateLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPosProportional, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&,
                                   const juce::Colour& backgroundColour,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown) override;

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getLabelFont (juce::Label&) override;

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    };
}
