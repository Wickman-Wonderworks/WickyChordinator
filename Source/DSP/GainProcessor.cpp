#include "DSP/GainProcessor.h"

namespace boilerplate::dsp
{
    void GainProcessor::prepare (double sampleRate, float initialGainDecibels)
    {
        jassert (sampleRate > 0.0);

        smoothedGain.reset (sampleRate, rampSeconds);
        smoothedGain.setCurrentAndTargetValue (
            juce::Decibels::decibelsToGain (initialGainDecibels, minDecibels));
    }

    void GainProcessor::reset() noexcept
    {
        smoothedGain.setCurrentAndTargetValue (1.0f);
    }

    void GainProcessor::process (juce::AudioBuffer<float>& buffer,
                                 float gainDecibels,
                                 bool bypassed) noexcept
    {
        // Bypass ramps to unity rather than hard-switching, which avoids a click
        // when the host toggles the parameter mid-playback.
        const auto targetGain = bypassed
            ? 1.0f
            : juce::Decibels::decibelsToGain (gainDecibels, minDecibels);

        smoothedGain.setTargetValue (targetGain);
        smoothedGain.applyGain (buffer, buffer.getNumSamples());
    }
}
