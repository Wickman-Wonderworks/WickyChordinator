#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace boilerplate::dsp
{
    /**
        Block-based output gain stage with a click-free bypass and parameter ramp.

        The processor performs no allocation after prepare() and touches no
        message-thread state, so it is safe to call from the audio thread.
    */
    class GainProcessor
    {
    public:
        static constexpr float minDecibels = -100.0f;
        static constexpr double rampSeconds = 0.02;

        /** Prepares the ramp for the current sample rate and snaps to the initial value. */
        void prepare (double sampleRate, float initialGainDecibels, bool initiallyBypassed);

        /** Restores unity gain and clears the ramp state. */
        void reset() noexcept;

        /** Applies smoothed gain to every channel of the buffer. */
        void process (juce::AudioBuffer<float>& buffer,
                      float gainDecibels,
                      bool bypassed) noexcept;

    private:
        float cachedGainDecibels = 0.0f;
        float cachedLinearGain = 1.0f;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain { 1.0f };
    };
}
