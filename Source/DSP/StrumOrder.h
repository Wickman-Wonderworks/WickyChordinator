#pragma once

#include <juce_core/juce_core.h>

#include <utility>

namespace boilerplate::dsp::strum
{
    inline constexpr int maxChordTones = 8;

    /**
        The ordering strategies for one generated chord.

        This is a finite ordering of a single chord, not a repeating arpeggiator
        or sequencer. The parameter stores an index into patternChoices; the
        order of that array is a session/preset contract and must never change.
    */
    enum class Pattern
    {
        legacyDirection,
        alternate,
        random,
        outsideIn
    };

    struct PatternChoice
    {
        const char* name;
        Pattern pattern;
    };

    inline constexpr int numPatterns = 4;

    // Frozen choice order: Legacy Direction (default), Alternate, Random,
    // Outside-In. Append new strategies at the end; never reorder or remove.
    inline constexpr PatternChoice patternChoices[numPatterns] =
    {
        { "Legacy Direction", Pattern::legacyDirection },
        { "Alternate",        Pattern::alternate },
        { "Random",           Pattern::random },
        { "Outside-In",       Pattern::outsideIn }
    };

    inline constexpr int legacyDirectionPattern = 0;
    inline constexpr int alternatePattern = 1;
    inline constexpr int randomPattern = 2;
    inline constexpr int outsideInPattern = 3;
    inline constexpr int defaultPatternIndex = legacyDirectionPattern;

    /** Display names for AudioParameterChoice, in the frozen table order.
        Message-thread only: it builds a StringArray and must never be called
        from processBlock.
    */
    inline juce::StringArray patternNames()
    {
        juce::StringArray names;

        for (const auto& choice : patternChoices)
            names.add (choice.name);

        return names;
    }

    /**
        Advances the engine's xorshift32 PRNG.

        The caller owns the state. This is the same sequence used by the
        processor's humanize PRNG, so passing the engine seed keeps random
        ordering reproducible.
    */
    inline juce::uint32 nextRandom (juce::uint32& state) noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;

        if (state == 0)
            state = 0x9e3779b9u;

        return state;
    }

    /**
        Fills order with a permutation of 0..size-1 for one chord.

        - Legacy Direction follows legacyDown (the released Up/Down choice).
        - Alternate starts Up, then toggles alternateDown on every trigger. Pass
          the state belonging to the chord's MIDI channel.
        - Random applies a bounded Fisher-Yates shuffle using randomState.
          randomState advances, so the caller must store it back.
        - Outside-In emits lowest, highest, second-lowest, second-highest, ...

        Non-legacy patterns ignore legacyDown. size is clamped to 0..8; callers
        with a larger chord receive only the first eight entries. The function
        allocates nothing and uses no global RNG.
    */
    inline void buildOrder (int* order, int size, int patternIndex, bool legacyDown,
                            bool& alternateDown, juce::uint32& randomState) noexcept
    {
        if (order == nullptr || size <= 0)
            return;

        size = juce::jlimit (0, maxChordTones, size);

        const auto pattern = patternChoices[
            juce::jlimit (0, numPatterns - 1, patternIndex)].pattern;

        switch (pattern)
        {
            case Pattern::legacyDirection:
                for (int i = 0; i < size; ++i)
                    order[i] = legacyDown ? (size - 1 - i) : i;
                break;

            case Pattern::alternate:
                for (int i = 0; i < size; ++i)
                    order[i] = alternateDown ? (size - 1 - i) : i;

                alternateDown = ! alternateDown;
                break;

            case Pattern::random:
            {
                int items[maxChordTones] {};

                for (int i = 0; i < size; ++i)
                    items[i] = i;

                for (int i = size - 1; i > 0; --i)
                {
                    const auto randomIndex = static_cast<int> (
                        nextRandom (randomState)
                        % static_cast<juce::uint32> (i + 1));
                    std::swap (items[i], items[randomIndex]);
                }

                for (int i = 0; i < size; ++i)
                    order[i] = items[i];
                break;
            }

            case Pattern::outsideIn:
            default:
            {
                int low = 0;
                int high = size - 1;

                for (int i = 0; i < size; ++i)
                    order[i] = (i & 1) == 0 ? low++ : high--;
                break;
            }
        }
    }
}
