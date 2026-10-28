#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace boilerplate::ui
{
    namespace palette
    {
        inline const juce::Colour panelTop    { 0xff25282d };
        inline const juce::Colour panelBottom { 0xff15171a };
        inline const juce::Colour frame       { 0xff3a3f46 };
        inline const juce::Colour line        { 0xff3c4149 };
        inline const juce::Colour cream       { 0xffe8e0cf };
        inline const juce::Colour muted       { 0xff9299a3 };
        inline const juce::Colour accent      { 0xffd9a441 };
        inline const juce::Colour accentDark  { 0xff5a4520 };
        inline const juce::Colour track       { 0xff4a5058 };
        inline const juce::Colour tick        { 0xff6a717b };
        inline const juce::Colour knobLight   { 0xff40464e };
        inline const juce::Colour knobDark    { 0xff1b1e22 };
        inline const juce::Colour knobEdge    { 0xff0e1012 };
        inline const juce::Colour control     { 0xff23272c };
        inline const juce::Colour controlEdge { 0xff555c66 };
        inline const juce::Colour ledOff      { 0xff3a3f46 };
        inline const juce::Colour screw       { 0xff707780 };
        inline const juce::Colour screwSlot   { 0xff1a1c1f };
    }

    /** Hardware-inspired look for the boilerplate editor. */
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

        juce::Font getLabelFont (juce::Label&) override;
    };
}
