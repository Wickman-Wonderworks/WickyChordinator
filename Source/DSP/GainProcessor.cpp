#include "DSP/GainProcessor.h"

namespace boilerplate::dsp
{
    void GainProcessor::prepare (double sampleRate, float initialGainDecibels, bool initiallyBypassed)
    {
        jassert (sampleRate > 0.0);

        cachedGainDecibels = initialGainDecibels;
        cachedLinearGain = juce::Decibels::decibelsToGain (initialGainDecibels, minDecibels);
        smoothedGain.reset (sampleRate, rampSeconds);
        smoothedGain.setCurrentAndTargetValue (initiallyBypassed ? 1.0f : cachedLinearGain);
    }

    void GainProcessor::reset() noexcept
    {
        smoothedGain.setCurrentAndTargetValue (1.0f);
        cachedGainDecibels = 0.0f;
        cachedLinearGain = 1.0f;
    }

    void GainProcessor::process (juce::AudioBuffer<float>& buffer,
                                 float gainDecibels,
                                 bool bypassed) noexcept
    {
        // Bypass ramps to unity rather than hard-switching, which avoids a click
        // when the host toggles the parameter mid-playback.
        // Exact comparison detects changes to the cached parameter, not numeric convergence.
        if (! juce::exactlyEqual (gainDecibels, cachedGainDecibels))
        {
            cachedGainDecibels = gainDecibels;
            cachedLinearGain = juce::Decibels::decibelsToGain (gainDecibels, minDecibels);
        }

        const auto targetGain = bypassed ? 1.0f : cachedLinearGain;

        smoothedGain.setTargetValue (targetGain);

        if (! smoothedGain.isSmoothing() && juce::exactlyEqual (targetGain, 1.0f))
            return;

        smoothedGain.applyGain (buffer, buffer.getNumSamples());
    }
}
