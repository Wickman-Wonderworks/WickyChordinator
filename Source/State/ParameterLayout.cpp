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
            ParameterID { gain, gainVersionHint },
            "Velocity Gain",
            NormalisableRange<float> { -60.0f, 12.0f, 0.1f },
            0.0f,
            gainAttributes));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { bypass, bypassVersionHint },
            "Bypass",
            false));

        // Chord Settings Matrix. The choice order is part of the state file
        // contract: append new choices at the end, never reorder existing ones.
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { chordType, chordTypeVersionHint },
            "Chord Type",
            StringArray { "Major", "Minor", "Dominant", "Other" },
            0));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { complexity, complexityVersionHint },
            "Complexity",
            StringArray { "Triad", "6th", "7th", "add9", "9th", "6over9",
                          "11th", "13th", "7b5", "9#11", "13#11" },
            0));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { voicing, voicingVersionHint },
            "Voicing",
            StringArray { "High", "Root", "Low" },
            1));

        // Per-chord transpose in semitones. The processor applies this to the
        // chord generated for the trigger note currently being played.
        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { pitchOffset, pitchOffsetVersionHint },
            "Pitch Offset",
            pitchOffsetMin,
            pitchOffsetMax,
            0,
            AudioParameterIntAttributes().withLabel ("st")));

        return layout;
    }
}
