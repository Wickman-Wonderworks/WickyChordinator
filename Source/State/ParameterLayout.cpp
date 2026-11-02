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

        // Global power switch. ON means the chord engine is armed; this is
        // deliberately separate from the host-facing Bypass parameter so both
        // automation and the front-panel switch remain meaningful.
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { power, powerVersionHint },
            "Power",
            true));

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

        //==========================================================================
        // Global interaction engine. Percentages keep the UI consistent and let
        // the processor map them to physical units with the current sample rate.
        //==========================================================================
        auto percentAttributes = AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float value, int)
            {
                return String (roundToInt (value)) + "%";
            });

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { spread, spreadVersionHint },
            "Spread",
            NormalisableRange<float> { 0.0f, 100.0f, 0.1f },
            0.0f,
            percentAttributes));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { spreadMode, spreadModeVersionHint },
            "Spread Mode",
            StringArray { "Dense", "Sparse" },
            0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { humanize, humanizeVersionHint },
            "Humanize",
            NormalisableRange<float> { 0.0f, 100.0f, 0.1f },
            0.0f,
            percentAttributes));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { noteOffMode, noteOffModeVersionHint },
            "Note Off Mode",
            StringArray { "Simultaneous", "Staggered" },
            0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { strum, strumVersionHint },
            "Strum",
            NormalisableRange<float> { 0.0f, 100.0f, 0.1f },
            0.0f,
            percentAttributes));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { strumMode, strumModeVersionHint },
            "Strum Mode",
            StringArray { "Up", "Down" },
            0));

        return layout;
    }
}
