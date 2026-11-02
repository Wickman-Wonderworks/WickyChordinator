#pragma once

#include <juce_graphics/juce_graphics.h>

namespace boilerplate::ui::fonts
{
    /** Returns the embedded Oxanium typeface at the requested weight. */
    juce::Typeface::Ptr oxaniumTypeface (float weight = 400.0f);

    /** Creates an Oxanium font at the requested height and weight. */
    juce::Font oxanium (float height, float weight = 400.0f);

    /** Installs Oxanium as the application-wide default sans-serif face. */
    void initialiseOxanium();
}
