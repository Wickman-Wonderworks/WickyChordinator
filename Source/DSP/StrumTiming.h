#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace boilerplate::dsp::strum
{
    /**
        One musical division expressed in quarter-note beats.

        The choice order below is a session/preset contract: an index stored in
        a preset or AU state must keep pointing at the same division forever.
        Append new divisions at the end instead of reordering or removing any
        existing entry.
    */
    struct Division
    {
        const char* name;
        double quarterNoteBeats;
    };

    inline constexpr int numDivisions = 9;

    // Straight values first, then their dotted (x1.5) and triplet (x2/3)
    // forms. Straight eighth 0.5, sixteenth 0.25, thirty-second 0.125.
    inline constexpr Division divisions[numDivisions] =
    {
        { "1/8",          0.5 },
        { "1/8 Dotted",   0.5 * 1.5 },
        { "1/8 Triplet",  0.5 * 2.0 / 3.0 },
        { "1/16",         0.25 },
        { "1/16 Dotted",  0.25 * 1.5 },
        { "1/16 Triplet", 0.25 * 2.0 / 3.0 },
        { "1/32",         0.125 },
        { "1/32 Dotted",  0.125 * 1.5 },
        { "1/32 Triplet", 0.125 * 2.0 / 3.0 }
    };

    /** Shipped default: a straight sixteenth note. */
    inline constexpr int defaultDivisionIndex = 3;

    /** Tempo used when the host supplies no usable tempo. */
    inline constexpr double fallbackBpm = 120.0;

    /**
        Host tempos are clamped to this range before conversion.

        The manual fallback parameter has its own released range of 20-300 BPM.
        That range constrains the fallback control, not valid host tempos, so
        the host path accepts anything from 1 to 1000 BPM and treats non-finite
        or non-positive values as absent.
    */
    inline constexpr double minHostBpm = 1.0;
    inline constexpr double maxHostBpm = 1000.0;

    /**
        Longest step this converter will emit.

        A deliberately slow host tempo is clamped here so the resulting sample
        offset stays bounded. Ten seconds is far beyond any useful strum step
        and still fits comfortably in the scheduler's 32-bit offsets.
    */
    inline constexpr double maxStepSeconds = 10.0;

    /** Released free-time strum range: 100% is 60 ms between chord tones. */
    inline constexpr float maxFreeStepSeconds = 0.060f;

    /**
        Hard sample-count cap.

        This protects the conversion and the scheduler's 32-bit sample offsets
        from an absurd but finite sample rate. It is not normally reached:
        10 seconds at 768 kHz is 7,680,000 samples.
    */
    inline constexpr int maxStepSamples = 8 * 1024 * 1024;

    /** True when the sample clock can produce a meaningful duration. */
    inline bool isValidSampleRate (double sampleRate) noexcept
    {
        return std::isfinite (sampleRate) && sampleRate > 0.0;
    }

    /** True when a tempo is finite and positive. */
    inline bool isValidBpm (double bpm) noexcept
    {
        return std::isfinite (bpm) && bpm > 0.0;
    }

    /** Returns a usable tempo, clamped to the documented host bounds. */
    inline double sanitizedBpm (double bpm) noexcept
    {
        if (! isValidBpm (bpm))
            return fallbackBpm;

        return std::clamp (bpm, minHostBpm, maxHostBpm);
    }

    /** Rounds a non-negative sample count into the bounded integer range. */
    inline int toStepSamples (double samples) noexcept
    {
        if (std::isnan (samples) || samples <= 0.0)
            return 0;

        // std::min also saturates a positive infinity to the hard cap.
        return juce::roundToInt (std::min (samples,
                                           static_cast<double> (maxStepSamples)));
    }

    /**
        stepSamples = round(sampleRate * 60 / bpm * quarterNoteBeats)

        Invalid sample rates return zero (there is no usable clock). Invalid,
        zero or non-finite tempos use 120 BPM. Durations longer than
        maxStepSeconds are clamped.
    */
    inline int stepSamplesForBeats (double sampleRate, double bpm,
                                    double quarterNoteBeats) noexcept
    {
        if (! isValidSampleRate (sampleRate)
            || ! std::isfinite (quarterNoteBeats) || quarterNoteBeats <= 0.0)
            return 0;

        const auto seconds = 60.0 / sanitizedBpm (bpm) * quarterNoteBeats;
        return toStepSamples (std::min (seconds, maxStepSeconds) * sampleRate);
    }

    /**
        Legacy free-time mapping.

        This reproduces the released Strum parameter exactly: a percentage of
        the 60 ms maximum, rounded with the same JUCE helper the processor has
        always used. Free mode therefore sounds unchanged.
    */
    inline int freeStepSamples (double sampleRate, float strumPercent) noexcept
    {
        if (! isValidSampleRate (sampleRate))
            return 0;

        const auto amount = juce::jlimit (0.0f, 100.0f, strumPercent) / 100.0f;
        return toStepSamples (static_cast<double> (amount * maxFreeStepSeconds) * sampleRate);
    }

    /** Synced step for a division index; out-of-range indices are clamped. */
    inline int syncStepSamples (double sampleRate, double bpm,
                                int divisionIndex) noexcept
    {
        const auto index = juce::jlimit (0, numDivisions - 1, divisionIndex);
        return stepSamplesForBeats (sampleRate, bpm, divisions[index].quarterNoteBeats);
    }

    /** Selects the free-time or synced mapping for one block. */
    inline int stepSamples (bool sync, double sampleRate, float strumPercent,
                            double bpm, int divisionIndex) noexcept
    {
        return sync ? syncStepSamples (sampleRate, bpm, divisionIndex)
                    : freeStepSamples (sampleRate, strumPercent);
    }

    /** Display names for AudioParameterChoice, in the frozen table order.
        Message-thread only: it builds a StringArray and must never be called
        from processBlock.
    */
    inline juce::StringArray divisionNames()
    {
        juce::StringArray names;

        for (const auto& division : divisions)
            names.add (division.name);

        return names;
    }
}
