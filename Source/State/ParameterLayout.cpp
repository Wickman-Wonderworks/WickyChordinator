#include "State/ParameterLayout.h"
#include "State/ParameterIDs.h"

#include <memory>

namespace boilerplate::parameters
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;

        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        auto gainAttributes = AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float value, int)
            {
                return String (value, 1) + " dB";
            });

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { gain, versionHint },
            "Output Gain",
            NormalisableRange<float> { -60.0f, 12.0f, 0.1f },
            0.0f,
            gainAttributes));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { bypass, versionHint },
            "Bypass",
            false));

        return layout;
    }
}
