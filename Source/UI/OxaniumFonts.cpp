#include <juce_gui_basics/juce_gui_basics.h>

#include "UI/OxaniumFonts.h"

#include <array>

#include "BinaryData.h"

namespace boilerplate::ui::fonts
{
    namespace
    {
        juce::Typeface::Ptr masterTypeface()
        {
            static auto typeface = juce::Typeface::createSystemTypefaceFor (
                BinaryData::OxaniumVariable_ttf,
                static_cast<size_t> (BinaryData::OxaniumVariable_ttfSize));

            return typeface;
        }

        juce::Typeface::Ptr weightTypeface (float weight)
        {
            static const auto regular = []
            {
                if (auto master = masterTypeface())
                {
                    const std::array<juce::FontVariableSetting, 1> settings {
                        juce::FontVariableSetting { juce::FontFeatureTag ("wght"), 400.0f }
                    };

                    if (auto configured = master->cloneWithVariableSettings (settings))
                        return configured;
                }

                return masterTypeface();
            }();

            static const auto bold = []
            {
                if (auto master = masterTypeface())
                {
                    const std::array<juce::FontVariableSetting, 1> settings {
                        juce::FontVariableSetting { juce::FontFeatureTag ("wght"), 700.0f }
                    };

                    if (auto configured = master->cloneWithVariableSettings (settings))
                        return configured;
                }

                return masterTypeface();
            }();

            return weight >= 600.0f ? bold : regular;
        }
    }

    juce::Typeface::Ptr oxaniumTypeface (float weight)
    {
        return weightTypeface (weight);
    }

    juce::Font oxanium (float height, float weight)
    {
        return juce::Font (juce::FontOptions (weightTypeface (weight)).withHeight (height));
    }

    void initialiseOxanium()
    {
        static bool initialised = false;

        if (initialised)
            return;

        initialised = true;

        if (auto regular = oxaniumTypeface (400.0f))
            juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypeface (regular);
    }
}
