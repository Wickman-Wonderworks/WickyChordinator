#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/StrumOrder.h"
#include "DSP/StrumTiming.h"
#include "State/ParameterIDs.h"
#include "State/ParameterLayout.h"

#include <cstring>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr int kMaxChordTones = 8; // Matches PluginProcessor::maxChordTones.
    constexpr int chordTypeCount = 4;
    constexpr int complexityCount = 11;
    constexpr float minStaggerSecondsPerStep = 0.020f;
    constexpr int maxHumanizeVelocityDeviation = 15;

    struct ChordShape
    {
        int numTones = 0;
        int intervals[kMaxChordTones] {};
    };

    // Indexed by [chord type][complexity], matching ParameterLayout order:
    // Major, Minor, Dominant, Other x Triad, 6th, 7th, add9, 9th, 6over9,
    // 11th, 13th, 7b5, 9#11, 13#11. Intervals are semitones above the root and
    // must stay sorted ascending; the final entry is used as the chord span.
    // 7b5, 9#11 and 13#11 intentionally use the altered-dominant b7 in every
    // family because those names describe altered dominant chords.
    constexpr ChordShape chordShapes[chordTypeCount][complexityCount] =
    {
        { // Major
            { 3, { 0, 4, 7 } },                  // Triad
            { 4, { 0, 4, 7, 9 } },               // 6th
            { 4, { 0, 4, 7, 11 } },              // maj7
            { 4, { 0, 4, 7, 14 } },              // add9
            { 5, { 0, 4, 7, 11, 14 } },          // maj9
            { 5, { 0, 4, 7, 9, 14 } },           // 6/9
            { 6, { 0, 4, 7, 11, 14, 17 } },      // maj11
            { 6, { 0, 4, 7, 11, 14, 21 } },      // maj13
            { 4, { 0, 4, 6, 10 } },              // 7b5
            { 6, { 0, 4, 7, 10, 14, 18 } },      // 9#11
            { 7, { 0, 4, 7, 10, 14, 18, 21 } }   // 13#11
        },
        { // Minor
            { 3, { 0, 3, 7 } },
            { 4, { 0, 3, 7, 9 } },
            { 4, { 0, 3, 7, 10 } },
            { 4, { 0, 3, 7, 14 } },
            { 5, { 0, 3, 7, 10, 14 } },
            { 5, { 0, 3, 7, 9, 14 } },
            { 6, { 0, 3, 7, 10, 14, 17 } },
            { 6, { 0, 3, 7, 10, 14, 21 } },
            { 4, { 0, 3, 6, 10 } },              // m7b5
            { 6, { 0, 3, 7, 10, 14, 18 } },
            { 7, { 0, 3, 7, 10, 14, 18, 21 } }
        },
        { // Dominant
            { 3, { 0, 4, 7 } },
            { 4, { 0, 4, 7, 9 } },
            { 4, { 0, 4, 7, 10 } },
            { 4, { 0, 4, 7, 14 } },
            { 5, { 0, 4, 7, 10, 14 } },
            { 5, { 0, 4, 7, 9, 14 } },
            { 6, { 0, 4, 7, 10, 14, 17 } },
            { 6, { 0, 4, 7, 10, 14, 21 } },
            { 4, { 0, 4, 6, 10 } },
            { 6, { 0, 4, 7, 10, 14, 18 } },
            { 7, { 0, 4, 7, 10, 14, 18, 21 } }
        },
        { // Other: diminished family, plus the literal altered dominants.
            { 3, { 0, 3, 6 } },                  // diminished
            { 4, { 0, 3, 6, 9 } },               // dim7
            { 4, { 0, 3, 6, 10 } },              // half-diminished
            { 4, { 0, 3, 6, 14 } },
            { 5, { 0, 3, 6, 10, 14 } },
            { 5, { 0, 3, 6, 9, 14 } },
            { 6, { 0, 3, 6, 10, 14, 17 } },
            { 6, { 0, 3, 6, 10, 14, 21 } },
            { 4, { 0, 3, 6, 10 } },              // 7b5 / half-diminished
            { 6, { 0, 3, 6, 10, 14, 18 } },
            { 7, { 0, 3, 6, 10, 14, 18, 21 } }
        }
    };

    /** High / Root / Low are register choices relative to the played key. */
    int voicingOctaveOffset (int voicingIndex) noexcept
    {
        switch (voicingIndex)
        {
            case 0:  return 12;   // High
            case 2:  return -12;  // Low
            default: return 0;    // Root
        }
    }

    /**
        Spread opens the chord away from a close-position voicing.

        Dense keeps every chord tone and pushes the upper voices out gradually,
        while Sparse keeps the bass/root in place and lifts the remaining voices
        together. Both modes are exactly zero at Spread = 0 so existing presets
        and per-key recipes remain bit-for-bit compatible.
    */
    int spreadOffsetForNote (int noteIndex, int numNotes,
                             float spreadAmount, int spreadMode) noexcept
    {
        if (spreadAmount <= 0.0f || numNotes <= 1 || noteIndex <= 0)
            return 0;

        const auto amount = juce::jlimit (0.0f, 1.0f, spreadAmount);
        const auto octaves = amount < 0.34f ? 0 : (amount < 0.67f ? 1 : 2);

        if (octaves == 0)
            return 0;

        if (spreadMode == 1) // Sparse: bass stays put, upper voices lift.
            return octaves * 12;

        // Dense: progressively open the upper voices across the chord.
        const auto position = static_cast<float> (noteIndex)
                            / static_cast<float> (numNotes - 1);
        return juce::roundToInt (position * static_cast<float> (octaves)) * 12;
    }

    const juce::Identifier keyRecipesStateTag ("KeyRecipes");
    const juce::Identifier keyRecipeStateTag ("Key");

    /**
        Reads the host tempo once for the current block.

        The AudioPlayHead pointer must come from getPlayHead() inside
        processBlock. This function neither stores the pointer nor the position
        object. Absent or unusable tempo data returns zero so the caller can
        substitute the manual fallback.
    */
    double readHostTempoBpm (juce::AudioPlayHead* playHead) noexcept
    {
        if (playHead == nullptr)
            return 0.0;

        const auto position = playHead->getPosition();

        if (! position.hasValue())
            return 0.0;

        const auto bpm = position->getBpm();

        if (! bpm.hasValue())
            return 0.0;

        const auto value = *bpm;
        return std::isfinite (value) && value > 0.0 ? value : 0.0;
    }

    static_assert (boilerplate::parameters::pitchOffsetMax
                       - boilerplate::parameters::pitchOffsetMin <= 31,
                   "Pitch offset no longer fits in the packed key recipe");
}


PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
         #if ! JucePlugin_IsSynth
          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
         #endif
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", boilerplate::parameters::createLayout())
{
    gainDbParameter = apvts.getRawParameterValue (boilerplate::parameters::gain);
    bypassParameter = apvts.getRawParameterValue (boilerplate::parameters::bypass);
    bypassParameterObject = apvts.getParameter (boilerplate::parameters::bypass);

    // Chord Settings Matrix: cache once, immediately after the APVTS exists.
    typeParam = apvts.getRawParameterValue (boilerplate::parameters::chordType);
    complexityParam = apvts.getRawParameterValue (boilerplate::parameters::complexity);
    voicingParam = apvts.getRawParameterValue (boilerplate::parameters::voicing);
    pitchOffsetParam = apvts.getRawParameterValue (boilerplate::parameters::pitchOffset);

    // Global interaction engine: same lock-free caching contract.
    powerParam = apvts.getRawParameterValue (boilerplate::parameters::power);
    spreadParam = apvts.getRawParameterValue (boilerplate::parameters::spread);
    spreadModeParam = apvts.getRawParameterValue (boilerplate::parameters::spreadMode);
    humanizeParam = apvts.getRawParameterValue (boilerplate::parameters::humanize);
    noteOffModeParam = apvts.getRawParameterValue (boilerplate::parameters::noteOffMode);
    strumParam = apvts.getRawParameterValue (boilerplate::parameters::strum);
    strumModeParam = apvts.getRawParameterValue (boilerplate::parameters::strumMode);
    strumTimingModeParam = apvts.getRawParameterValue (boilerplate::parameters::strumTimingMode);
    strumDivisionParam = apvts.getRawParameterValue (boilerplate::parameters::strumDivision);
    strumFallbackBpmParam = apvts.getRawParameterValue (boilerplate::parameters::strumFallbackBpm);
    strumPatternParam = apvts.getRawParameterValue (boilerplate::parameters::strumPattern);
    voicingStyleParam = apvts.getRawParameterValue (boilerplate::parameters::voicingStyle);
    humanizeTimingParam = apvts.getRawParameterValue (boilerplate::parameters::humanizeTiming);
    scaleRootParam = apvts.getRawParameterValue (boilerplate::parameters::scaleRoot);
    scaleModeParam = apvts.getRawParameterValue (boilerplate::parameters::scaleMode);

    jassert (gainDbParameter != nullptr);
    jassert (bypassParameter != nullptr);
    jassert (bypassParameterObject != nullptr);
    jassert (typeParam != nullptr);
    jassert (complexityParam != nullptr);
    jassert (voicingParam != nullptr);
    jassert (pitchOffsetParam != nullptr);
    jassert (powerParam != nullptr);
    jassert (spreadParam != nullptr);
    jassert (spreadModeParam != nullptr);
    jassert (humanizeParam != nullptr);
    jassert (noteOffModeParam != nullptr);
    jassert (strumParam != nullptr);
    jassert (strumModeParam != nullptr);
    jassert (strumTimingModeParam != nullptr);
    jassert (strumDivisionParam != nullptr);
    jassert (strumFallbackBpmParam != nullptr);
    jassert (strumPatternParam != nullptr);

    for (auto& recipe : keyRecipes)
        recipe.store (0, std::memory_order_relaxed);

    for (auto& word : activeInputNotes)
        word.store (0, std::memory_order_relaxed);

    humanizeSeed.store (0x9e3779b9u, std::memory_order_relaxed);

    // Reserved on the message thread so the audio callback never grows it.
    keyboardEventBuffer.ensureSize (keyboardEventBufferBytes);
}

PluginProcessor::~PluginProcessor() = default;

//==============================================================================
const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
    return JucePlugin_WantsMidiInput != 0;
}

bool PluginProcessor::producesMidi() const
{
    return JucePlugin_ProducesMidiOutput != 0;
}

bool PluginProcessor::isMidiEffect() const
{
    return JucePlugin_IsMidiEffect != 0;
}

double PluginProcessor::getTailLengthSeconds() const
{
    // Free-time strum plus staggered release fit within one second. Synced
    // steps can exceed this at very slow host tempos; hosts that truncate MIDI
    // tails for long synced strums are validated separately.
    return 1.0;
}

juce::AudioProcessorParameter* PluginProcessor::getBypassParameter() const
{
    return bypassParameterObject;
}

//==============================================================================
juce::uint64 PluginProcessor::getActiveInputNotes (const int wordIndex) const noexcept
{
    if (! juce::isPositiveAndBelow (wordIndex, 2))
        return 0;

    return activeInputNotes[wordIndex].load (std::memory_order_relaxed);
}

juce::uint64 PluginProcessor::getActiveOutputNotes (const int wordIndex) const noexcept
{
    if (! juce::isPositiveAndBelow (wordIndex, 2))
        return 0;

    return activeOutputNotes[wordIndex].load (std::memory_order_relaxed);
}

int PluginProcessor::getLastTriggeredChordNote() const noexcept
{
    return lastTriggeredChordNote.load (std::memory_order_relaxed);
}

PluginProcessor::ChordRecipe PluginProcessor::getLastTriggeredChordRecipe() const noexcept
{
    return unpackKeyRecipe (lastTriggeredRecipe.load (std::memory_order_relaxed));
}

int PluginProcessor::getNumSoundingChords() const noexcept
{
    return soundingChordCount.load (std::memory_order_relaxed);
}

bool PluginProcessor::isUsingHostTempo() const noexcept
{
    return hostTempoActive.load (std::memory_order_relaxed);
}

float PluginProcessor::getEffectiveStrumTempo() const noexcept
{
    return effectiveStrumTempoBpm.load (std::memory_order_relaxed);
}

void PluginProcessor::queueKeyboardNote (const bool noteOn, const int midiNote,
                                         const float velocity) noexcept
{
    if (! juce::isPositiveAndBelow (midiNote, 128))
        return;

    const auto status = static_cast<juce::uint8> (noteOn ? 0x90 : 0x80);
    const auto data2 = static_cast<juce::uint8> (
        noteOn ? juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f))
               : 0);

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    keyboardEventFifo.prepareToWrite (1, start1, size1, start2, size2);

    if (size1 > 0)
    {
        keyboardEvents[start1].data[0] = status;
        keyboardEvents[start1].data[1] = static_cast<juce::uint8> (midiNote);
        keyboardEvents[start1].data[2] = data2;
        keyboardEventFifo.finishedWrite (1);
    }
    else
        keyboardOverflow.store (true, std::memory_order_relaxed);
}

void PluginProcessor::drainKeyboardEvents() noexcept
{
    const auto ready = keyboardEventFifo.getNumReady();

    if (ready <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    keyboardEventFifo.prepareToRead (ready, start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
        keyboardEventBuffer.addEvent (keyboardEvents[start1 + i].data, 3, 0);

    for (int i = 0; i < size2; ++i)
        keyboardEventBuffer.addEvent (keyboardEvents[start2 + i].data, 3, 0);

    keyboardEventFifo.finishedRead (size1 + size2);
}

void PluginProcessor::clearInputNotes() noexcept
{
    std::memset (inputNotesByChannel, 0, sizeof (inputNotesByChannel));
    for (auto& word : activeInputNotes)
        word.store (0, std::memory_order_relaxed);
}

void PluginProcessor::clearOutputNotes() noexcept
{
    std::memset (outputNotesByChannel, 0, sizeof (outputNotesByChannel));
    for (auto& word : activeOutputNotes)
        word.store (0, std::memory_order_relaxed);
}

void PluginProcessor::updateNoteMask (const juce::uint8* data, int numBytes,
                                      bool (&notes)[16][128],
                                      std::atomic<juce::uint64> (&mask)[2]) noexcept
{
    if (numBytes != 3 || data == nullptr || data[1] > 127 || data[2] > 127)
        return;
    const auto status = data[0] & 0xf0;
    const auto channel = data[0] & 0x0f;
    if (status == 0x90 || status == 0x80)
        notes[channel][data[1]] = status == 0x90 && data[2] != 0;
    else if (status == 0xb0 && (data[1] == 120 || data[1] == 123))
        std::memset (notes[channel], 0, sizeof (notes[channel]));
    else
        return;

    // A note event only changes one pitch; CC120/123 may change all pitches.
    const int first = status == 0xb0 ? 0 : data[1];
    const int last = status == 0xb0 ? 127 : data[1];
    for (int note = first; note <= last; ++note)
    {
        bool held = false;
        for (int ch = 0; ch < 16; ++ch)
            held = held || notes[ch][note];
        const auto bit = juce::uint64 (1) << (note % 64);
        if (held)
            mask[note / 64].fetch_or (bit, std::memory_order_relaxed);
        else
            mask[note / 64].fetch_and (~bit, std::memory_order_relaxed);
    }
}

void PluginProcessor::updateInputNoteMask (const juce::uint8* data, int numBytes) noexcept
{
    updateNoteMask (data, numBytes, inputNotesByChannel, activeInputNotes);
}

void PluginProcessor::updateOutputNoteMask (const juce::uint8* data, int numBytes) noexcept
{
    updateNoteMask (data, numBytes, outputNotesByChannel, activeOutputNotes);
}

//==============================================================================
int PluginProcessor::getNumPrograms()
{
    return 1;
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String PluginProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void PluginProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);

    // This reservation stays owned by the processor for its entire lifetime.
    midiOutputBuffer.clear();
    midiOutputBuffer.ensureSize (midiOutputBytes);
    midiOutputBuffer.data.resize (midiOutputBytes); // Touch reserved pages before rendering.
    midiOutputBuffer.clear();

    keyboardEventBuffer.clear();
    keyboardEventBuffer.ensureSize (keyboardEventBufferBytes);
    keyboardEventFifo.reset();
    keyboardOverflow.store (false, std::memory_order_relaxed);

    // Re-seed the relaxed PRNG at preparation time. The audio thread only ever
    // performs relaxed loads/stores on this atomic, so no lock or allocation is
    // introduced into processBlock.
    humanizeSeed.store (static_cast<juce::uint32> (juce::Random::getSystemRandom().nextInt()),
                        std::memory_order_relaxed);

    totalSamplesProcessed = 0;
    resetChordState();
    wasBypassed = bypassParameter != nullptr && bypassParameter->load() > 0.5f;

    // The UI status reflects the last processed block. Until one runs, report
    // the fallback rather than a stale host tempo from a previous session.
    hostTempoActive.store (false, std::memory_order_relaxed);
    effectiveStrumTempoBpm.store (
        static_cast<float> (boilerplate::parameters::strumFallbackBpmDefault),
        std::memory_order_relaxed);
}

void PluginProcessor::releaseResources()
{
    midiOutputBuffer.clear();
    resetChordState();
}

void PluginProcessor::reset()
{
    resetChordState();
    wasBypassed = false;
}

void PluginProcessor::resetChordState() noexcept
{
    for (auto& chord : activeChords)
        chord = {};

    for (auto& pending : pendingNotes)
        pending = {};

    std::memset (noteRefCounts, 0, sizeof (noteRefCounts));
    std::memset (passThroughNotes, 0, sizeof (passThroughNotes));
    std::memset (previousVoicingCount, 0, sizeof (previousVoicingCount));
    std::memset (alternateDown, 0, sizeof (alternateDown));
    clearInputNotes();
    clearOutputNotes();

    lastTriggeredChordNote.store (-1, std::memory_order_relaxed);
    lastTriggeredRecipe.store (0, std::memory_order_relaxed);
    soundingChordCount.store (0, std::memory_order_relaxed);
    totalSamplesProcessed = 0;
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto outputChannels = layouts.getMainOutputChannelSet();

    if (outputChannels != juce::AudioChannelSet::mono()
        && outputChannels != juce::AudioChannelSet::stereo())
        return false;

   #if JucePlugin_IsSynth
    return layouts.inputBuses.isEmpty();
   #else
    return outputChannels == layouts.getMainInputChannelSet();
   #endif
}

//==============================================================================
juce::uint32 PluginProcessor::packKeyRecipe (const ChordRecipe& recipe) noexcept
{
    const auto type = static_cast<juce::uint32> (
        juce::jlimit (0, chordTypeCount - 1, recipe.chordType));
    const auto complexity = static_cast<juce::uint32> (
        juce::jlimit (0, complexityCount - 1, recipe.complexity));
    const auto voicing = static_cast<juce::uint32> (
        juce::jlimit (0, 2, recipe.voicing));
    const auto pitch = static_cast<juce::uint32> (
        juce::jlimit (boilerplate::parameters::pitchOffsetMin,
                      boilerplate::parameters::pitchOffsetMax,
                      recipe.pitchOffset)
        - boilerplate::parameters::pitchOffsetMin);

    return keyRecipeOverrideBit | type | (complexity << 2) | (voicing << 6) | (pitch << 8);
}

PluginProcessor::ChordRecipe PluginProcessor::unpackKeyRecipe (juce::uint32 packed) noexcept
{
    ChordRecipe recipe;
    recipe.chordType = static_cast<int> (packed & 0x3u);
    recipe.complexity = static_cast<int> ((packed >> 2) & 0xfu);
    recipe.voicing = static_cast<int> ((packed >> 6) & 0x3u);
    recipe.pitchOffset = static_cast<int> ((packed >> 8) & 0x1fu)
                       + boilerplate::parameters::pitchOffsetMin;
    return recipe;
}

void PluginProcessor::setKeyRecipe (int midiNote, ChordRecipe recipe) noexcept
{
    if (! juce::isPositiveAndBelow (midiNote, 128))
        return;

    keyRecipes[midiNote].store (packKeyRecipe (recipe), std::memory_order_release);
}

void PluginProcessor::clearKeyRecipe (int midiNote) noexcept
{
    if (! juce::isPositiveAndBelow (midiNote, 128))
        return;

    keyRecipes[midiNote].store (0, std::memory_order_release);
}

bool PluginProcessor::hasKeyRecipe (int midiNote) const noexcept
{
    return juce::isPositiveAndBelow (midiNote, 128)
        && (keyRecipes[midiNote].load (std::memory_order_acquire)
            & keyRecipeOverrideBit) != 0;
}

PluginProcessor::ChordRecipe PluginProcessor::getKeyRecipe (int midiNote) const noexcept
{
    if (juce::isPositiveAndBelow (midiNote, 128))
    {
        const auto packed = keyRecipes[midiNote].load (std::memory_order_acquire);

        if ((packed & keyRecipeOverrideBit) != 0)
            return unpackKeyRecipe (packed);
        const auto pitchClass = pitchClassRecipes[midiNote % 12].load (std::memory_order_acquire);
        if ((pitchClass & keyRecipeOverrideBit) != 0)
            return unpackKeyRecipe (pitchClass);
    }

    // Unassigned key: report the current global matrix values.
    ChordRecipe fallback;
    fallback.chordType = juce::jlimit (0, chordTypeCount - 1,
        juce::roundToInt (typeParam->load (std::memory_order_relaxed)));
    fallback.complexity = juce::jlimit (0, complexityCount - 1,
        juce::roundToInt (complexityParam->load (std::memory_order_relaxed)));
    fallback.voicing = juce::jlimit (0, 2,
        juce::roundToInt (voicingParam->load (std::memory_order_relaxed)));
    fallback.pitchOffset = juce::jlimit (boilerplate::parameters::pitchOffsetMin,
                                         boilerplate::parameters::pitchOffsetMax,
                                         juce::roundToInt (pitchOffsetParam->load (std::memory_order_relaxed)));
    return fallback;
}

void PluginProcessor::setPitchClassRecipe (int pitchClass, ChordRecipe recipe) noexcept
{
    if (juce::isPositiveAndBelow (pitchClass, 12))
        pitchClassRecipes[pitchClass].store (packKeyRecipe (recipe), std::memory_order_release);
}
void PluginProcessor::clearPitchClassRecipe (int pitchClass) noexcept
{
    if (juce::isPositiveAndBelow (pitchClass, 12))
        pitchClassRecipes[pitchClass].store (0, std::memory_order_release);
}
bool PluginProcessor::hasPitchClassRecipe (int pitchClass) const noexcept
{
    return juce::isPositiveAndBelow (pitchClass, 12)
        && (pitchClassRecipes[pitchClass].load() & keyRecipeOverrideBit) != 0;
}
PluginProcessor::ChordRecipe PluginProcessor::getPitchClassRecipe (int pitchClass) const noexcept
{
    return hasPitchClassRecipe (pitchClass) ? unpackKeyRecipe (pitchClassRecipes[pitchClass].load())
                                          : ChordRecipe {};
}

//==============================================================================
int PluginProcessor::buildChordNotes (int triggerNote, int chordType, int complexity,
                                      int voicing, int pitchOffset,
                                      float spreadAmount, int spreadMode,
                                      juce::uint8* notesOut) const noexcept
{
    if (notesOut == nullptr)
        return 0;

    chordType = juce::jlimit (0, chordTypeCount - 1, chordType);
    complexity = juce::jlimit (0, complexityCount - 1, complexity);
    voicing = juce::jlimit (0, 2, voicing);
    pitchOffset = juce::jlimit (boilerplate::parameters::pitchOffsetMin,
                                boilerplate::parameters::pitchOffsetMax,
                                pitchOffset);
    spreadMode = juce::jlimit (0, 1, spreadMode);

    const auto& shape = chordShapes[chordType][complexity];
    const auto numTones = juce::jlimit (0, maxChordTones, shape.numTones);

    if (numTones == 0)
        return 0;

    int spreadOffsets[maxChordTones] {};
    int maxSpreadOffset = 0;

    for (int i = 0; i < numTones; ++i)
    {
        spreadOffsets[i] = spreadOffsetForNote (i, numTones, spreadAmount, spreadMode);
        maxSpreadOffset = juce::jmax (maxSpreadOffset, spreadOffsets[i]);
    }

    // Per-key assignment: the trigger key is the chord root, so different keys
    // and octaves generate different chords from the same matrix state.
    int base = triggerNote + pitchOffset
             + (currentVoicingStyle == 0 ? voicingOctaveOffset (voicing) : 0);

    // Keep the whole shape inside the MIDI range by moving it in octaves. This
    // preserves the intervals instead of folding individual notes into a
    // different inversion.
    const auto highestInterval = shape.intervals[numTones - 1] + maxSpreadOffset;

    while (base < 0)
        base += 12;

    while (base + highestInterval > 127)
        base -= 12;

    if (base < 0 || base > 127)
        return 0;

    int count = 0;
    int scaledNotes[maxChordTones] {};

    for (int i = 0; i < numTones; ++i)
    {
        auto note = base + shape.intervals[i] + spreadOffsets[i];
        // Map C-major white-note degrees into the selected natural-minor scale.
        // Accidentals retain their chromatic position; root transposes all notes.
        if (currentScaleMode == 2)
        {
            const auto pc = note % 12;
            if (pc == 4 || pc == 9 || pc == 11)
                --note;
        }
        note += currentScaleRoot;
        scaledNotes[count++] = note;
    }

    while (count > 0 && scaledNotes[count - 1] > 127)
        for (int i = 0; i < count; ++i)
            scaledNotes[i] -= 12;
    for (int i = 0; i < count; ++i)
        notesOut[i] = static_cast<juce::uint8> (scaledNotes[i]);
    return count;
}

//==============================================================================
juce::uint32 PluginProcessor::nextRandom() noexcept
{
    auto state = humanizeSeed.load (std::memory_order_relaxed);
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;

    if (state == 0)
        state = 0x9e3779b9u;

    humanizeSeed.store (state, std::memory_order_relaxed);
    return state;
}

juce::uint8 PluginProcessor::humanizedVelocity (juce::uint8 velocity) noexcept
{
    const auto deviation = juce::roundToInt (currentHumanizeAmount
                                             * static_cast<float> (maxHumanizeVelocityDeviation));

    if (deviation <= 0)
        return velocity;

    const auto span = static_cast<juce::uint32> (deviation * 2 + 1);
    const auto offset = static_cast<int> (nextRandom() % span) - deviation;

    return static_cast<juce::uint8> (
        juce::jlimit (1, 127, static_cast<int> (velocity) + offset));
}

//==============================================================================
void PluginProcessor::emitNoteOn (int chordIndex, int noteIndex, int note,
                                  int channel, juce::uint8 velocity,
                                  int samplePosition, juce::MidiBuffer& output) noexcept
{
    if (! juce::isPositiveAndBelow (channel, midiChannels)
        || ! juce::isPositiveAndBelow (note, 128))
        return;

    if (juce::isPositiveAndBelow (chordIndex, maxActiveChords))
    {
        auto& chord = activeChords[chordIndex];

        // The slot may have been stolen or released between scheduling and
        // dispatch; never resurrect a dead chord.
        if (chord.triggerNote < 0
            || ! juce::isPositiveAndBelow (noteIndex, chord.numNotes)
            || chord.notes[noteIndex] != static_cast<juce::uint8> (note))
            return;

        chord.noteOnSent[noteIndex] = true;
    }

    auto& refCount = noteRefCounts[channel][note];

    if (refCount == 0 && ! passThroughNotes[channel][note])
        appendMidi (output, juce::MidiMessage::noteOn (channel + 1, note, velocity),
                         samplePosition);

    ++refCount;
}

void PluginProcessor::scheduleNoteOn (int chordIndex, int noteIndex, int note,
                                      int channel, juce::uint8 velocity,
                                      int targetSample,
                                      juce::MidiBuffer& output) noexcept
{
    targetSample = juce::jmax (0, targetSample);

    if (currentStrumStepSamples <= 0 && currentHumanizeMaxSamples <= 0)
    {
        emitNoteOn (chordIndex, noteIndex, note, channel, velocity, targetSample, output);
        return;
    }

    for (auto& pending : pendingNotes)
    {
        if (pending.active)
            continue;

        pending.chordIndex = chordIndex;
        pending.noteIndex = noteIndex;
        pending.channel = channel;
        pending.note = note;
        pending.velocity = velocity;
        pending.dueSample = totalSamplesProcessed + targetSample;
        pending.isNoteOn = true;
        pending.active = true;
        return;
    }

    // Queue exhausted: fall back to an immediate note so the key is never
    // silently swallowed.
    emitNoteOn (chordIndex, noteIndex, note, channel, velocity,
                juce::jlimit (0, juce::jmax (0, currentBlockSize - 1), targetSample), output);
}

void PluginProcessor::scheduleNoteOff (int channel, int note, int targetSample,
                                       juce::MidiBuffer& output) noexcept
{
    if (! juce::isPositiveAndBelow (channel, midiChannels)
        || ! juce::isPositiveAndBelow (note, 128))
        return;

    targetSample = juce::jmax (0, targetSample);

    for (auto& pending : pendingNotes)
    {
        if (pending.active)
            continue;

        pending.chordIndex = -1;
        pending.noteIndex = 0;
        pending.channel = channel;
        pending.note = note;
        pending.velocity = 0;
        pending.dueSample = totalSamplesProcessed + targetSample;
        pending.isNoteOn = false;
        pending.active = true;
        return;
    }

    // Queue pressure: release this ownership immediately rather than leak it.
    auto& count = noteRefCounts[channel][note];
    if (count > 0 && --count == 0 && ! passThroughNotes[channel][note])
        appendMidi (output, juce::MidiMessage::noteOff (channel + 1, note),
                    juce::jlimit (0, juce::jmax (0, currentBlockSize - 1), targetSample));
}

void PluginProcessor::processPendingNotes (juce::MidiBuffer& output, int endSample) noexcept
{
    // Dispatch strictly before endSample, in timestamp order (stable for ties).
    const auto end = totalSamplesProcessed + endSample;
    for (;;)
    {
        PendingNote* next = nullptr;
        for (auto& pending : pendingNotes)
            if (pending.active && pending.dueSample < end
                && (next == nullptr || pending.dueSample < next->dueSample))
                next = &pending;
        if (next == nullptr)
            return;
        const auto event = *next;
        next->active = false;
        const auto position = static_cast<int> (juce::jlimit<juce::int64> (
            0, juce::jmax (0, currentBlockSize - 1), event.dueSample - totalSamplesProcessed));
        if (event.isNoteOn)
        {
            if (! currentBypassState)
                emitNoteOn (event.chordIndex, event.noteIndex, event.note, event.channel,
                            event.velocity, position, output);
        }
        else
        {
            auto& count = noteRefCounts[event.channel][event.note];
            if (count > 0 && --count == 0 && ! passThroughNotes[event.channel][event.note])
                appendMidi (output, juce::MidiMessage::noteOff (event.channel + 1, event.note), position);
        }
    }
}

bool PluginProcessor::appendMidi (juce::MidiBuffer& output, const juce::MidiMessage& message,
                                  int position) noexcept
{
    return appendMidi (output, message.getRawData(), message.getRawDataSize(), position);
}

bool PluginProcessor::appendMidi (juce::MidiBuffer& output, const juce::uint8* bytes,
                                  int size, int position) noexcept
{
    // JUCE 9 MidiBuffer stores a 32-bit timestamp and 16-bit length per event.
    constexpr int overhead = sizeof (juce::int32) + sizeof (juce::uint16);
    if (size <= 0 || size > 65535 - overhead
        || size > midiOutputBytes - overhead - output.data.size())
    {
        midiOverflow = true;
        return false;
    }
    return output.addEvent (bytes, size, position);
}

void PluginProcessor::flushGeneratedNotes (juce::MidiBuffer& output, int position, int channel) noexcept
{
    const auto savedMode = currentNoteOffMode;
    currentNoteOffMode = 0;
    releaseAllChords (output, position, channel);
    currentNoteOffMode = savedMode;
    for (auto& pending : pendingNotes)
    {
        if (! pending.active || (channel >= 0 && pending.channel != channel))
            continue;
        if (! pending.isNoteOn)
        {
            auto& count = noteRefCounts[pending.channel][pending.note];
            if (count > 0 && --count == 0 && ! passThroughNotes[pending.channel][pending.note])
                appendMidi (output, juce::MidiMessage::noteOff (pending.channel + 1, pending.note), position);
        }
        pending.active = false;
    }
}

void PluginProcessor::cancelPendingNotesForChord (int chordIndex) noexcept
{
    for (auto& pending : pendingNotes)
        if (pending.active && pending.isNoteOn && pending.chordIndex == chordIndex)
            pending.active = false;
}

//==============================================================================
void PluginProcessor::applyVoicing (juce::uint8* notes, int count, int voicing, int channel) noexcept
{
    if (currentVoicingStyle == 0 || count <= 0)
        return;
    int best[8] {};
    int bestScore = std::numeric_limits<int>::max();
    const auto selected = currentVoicingStyle == 2 ? static_cast<int> (nextRandom() % 3) : voicing;
    for (int inversion = 0; inversion < 3; ++inversion)
    {
        // 0 = High (first inversion), 1 = Root, 2 = Low (previous inversion).
        if (currentVoicingStyle != 3 && inversion != selected)
            continue;
        int candidate[8] {};
        for (int i = 0; i < count; ++i)
            candidate[i] = notes[i];
        if (inversion == 0 && count > 1)
            candidate[0] += 12;
        else if (inversion == 2 && count > 1)
            while (candidate[count - 1] >= candidate[0])
                candidate[count - 1] -= 12;
        std::sort (candidate, candidate + count);
        for (int shift = -12; shift <= 12; shift += 12)
        {
            if (currentVoicingStyle != 3 && shift != 0)
                continue;
            if (candidate[0] + shift < 0 || candidate[count - 1] + shift > 127)
                continue;
            int score = 0;
            if (previousVoicingCount[channel] > 0 && currentVoicingStyle == 3)
                for (int i = 0; i < count; ++i)
                {
                    int distance = 128;
                    for (int j = 0; j < previousVoicingCount[channel]; ++j)
                        distance = juce::jmin (distance, std::abs (candidate[i] + shift - previousVoicing[channel][j]));
                    score += distance;
                }
            else
                score = std::abs (candidate[0] + shift - notes[0]);
            if (score < bestScore)
            {
                bestScore = score;
                for (int i = 0; i < count; ++i)
                    best[i] = candidate[i] + shift;
            }
        }
    }
    if (bestScore != std::numeric_limits<int>::max())
        for (int i = 0; i < count; ++i)
            notes[i] = static_cast<juce::uint8> (best[i]);
    previousVoicingCount[channel] = count;
    std::copy (notes, notes + count, previousVoicing[channel]);
}

bool PluginProcessor::startChord (int triggerNote, int channel, juce::uint8 velocity,
                                  int chordType, int complexity, int voicing, int pitchOffset,
                                  juce::MidiBuffer& output, int samplePosition) noexcept
{
    if (! juce::isPositiveAndBelow (channel, midiChannels))
        return false;

    // Re-pressing a held key must retire the old chord first. This also makes
    // the note list the single source of truth for what to release later.
    for (auto& chord : activeChords)
    {
        if (chord.triggerNote == triggerNote && chord.channel == channel)
        {
            stopChord (chord, output, samplePosition);
            break;
        }
    }

    ChordRecipe evaluatedRecipe;
    evaluatedRecipe.chordType = chordType;
    evaluatedRecipe.complexity = complexity;
    evaluatedRecipe.voicing = voicing;
    evaluatedRecipe.pitchOffset = pitchOffset;

    if (juce::isPositiveAndBelow (triggerNote, 128))
    {
        auto packed = keyRecipes[triggerNote].load (std::memory_order_acquire);
        if ((packed & keyRecipeOverrideBit) == 0)
            packed = pitchClassRecipes[triggerNote % 12].load (std::memory_order_acquire);

        if ((packed & keyRecipeOverrideBit) != 0)
            evaluatedRecipe = unpackKeyRecipe (packed);
    }


    juce::uint8 notes[maxChordTones] {};
    const auto numNotes = buildChordNotes (triggerNote, evaluatedRecipe.chordType, evaluatedRecipe.complexity,
                                           evaluatedRecipe.voicing, evaluatedRecipe.pitchOffset,
                                           currentSpreadAmount, currentSpreadMode,
                                           notes);

    if (numNotes == 0)
        return false;

    ActiveChord* slot = nullptr;

    for (auto& chord : activeChords)
    {
        if (chord.triggerNote < 0)
        {
            slot = &chord;
            break;
        }
    }

    if (slot == nullptr)
    {
        // Voice stealing: with 128 slots this is effectively unreachable, but
        // stealing keeps the "every note is tracked" invariant if a host sends
        // an unusually dense block.
        slot = &activeChords[0];
        stopChord (*slot, output, samplePosition);
    }

    applyVoicing (notes, numNotes, evaluatedRecipe.voicing, channel);

    const auto chordIndex = static_cast<int> (slot - activeChords);

    slot->triggerNote = triggerNote;
    slot->channel = channel;
    slot->numNotes = numNotes;

    for (int i = 0; i < numNotes; ++i)
    {
        slot->notes[i] = notes[i];
        slot->noteOnSent[i] = false;
    }

    soundingChordCount.fetch_add (1, std::memory_order_relaxed);
    lastTriggeredChordNote.store (triggerNote, std::memory_order_relaxed);

    lastTriggeredRecipe.store (packKeyRecipe (evaluatedRecipe),
                               std::memory_order_relaxed);

    // The ordering strategy is snapshotted for this chord, just like the step.
    // Legacy Direction follows Up/Down; the other patterns override it. The
    // helper allocates nothing and uses the engine PRNG for Random only.
    const auto strumStepSamples = currentStrumStepSamples;
    int noteOrder[maxChordTones] {};
    auto randomState = humanizeSeed.load (std::memory_order_relaxed);
    boilerplate::dsp::strum::buildOrder (noteOrder, numNotes, currentStrumPattern,
                                         currentStrumMode == 1,
                                         alternateDown[channel], randomState);
    humanizeSeed.store (randomState, std::memory_order_relaxed);

    for (int step = 0; step < numNotes; ++step)
    {
        const auto noteIndex = noteOrder[step];
        const auto jitter = currentHumanizeMaxSamples > 0
            ? static_cast<int> (nextRandom() % static_cast<juce::uint32> (currentHumanizeMaxSamples + 1)) : 0;
        const auto delaySamples = strumStepSamples * step + jitter;

        scheduleNoteOn (chordIndex, noteIndex, notes[noteIndex], channel,
                        humanizedVelocity (velocity),
                        samplePosition + delaySamples, output);
    }

    return true;
}

void PluginProcessor::stopChord (ActiveChord& chord, juce::MidiBuffer& output,
                                 int samplePosition) noexcept
{
    const auto chordIndex = static_cast<int> (&chord - activeChords);
    cancelPendingNotesForChord (chordIndex);

    if (! juce::isPositiveAndBelow (chord.channel, midiChannels))
    {
        chord = {};
        return;
    }

    const auto staggered = currentNoteOffMode == 1;
    const auto channel = chord.channel;
    const auto numNotes = chord.numNotes;

    for (int i = 0; i < numNotes; ++i)
    {
        if (! chord.noteOnSent[i])
            continue;

        const auto note = chord.notes[i];

        if (staggered)
        {
            scheduleNoteOff (channel, note,
                             samplePosition + i * currentStaggerStepSamples,
                             output);
        }
        else
        {
            auto& refCount = noteRefCounts[channel][note];

            if (refCount > 0)
            {
                --refCount;

                if (refCount == 0 && ! passThroughNotes[channel][note])
                    appendMidi (output, juce::MidiMessage::noteOff (channel + 1, note),
                                     samplePosition);
            }
        }
    }

    chord = {};
    soundingChordCount.fetch_sub (1, std::memory_order_relaxed);
}

int PluginProcessor::findActiveChord (int triggerNote, int channel) const noexcept
{
    for (int i = 0; i < maxActiveChords; ++i)
        if (activeChords[i].triggerNote == triggerNote
            && activeChords[i].channel == channel)
            return i;

    return -1;
}

bool PluginProcessor::isNoteActive (int channel, int note) const noexcept
{
    return juce::isPositiveAndBelow (channel, midiChannels)
        && juce::isPositiveAndBelow (note, 128)
        && noteRefCounts[channel][note] > 0;
}

void PluginProcessor::releaseAllChords (juce::MidiBuffer& output, int samplePosition,
                                        int channel) noexcept
{
    for (auto& chord : activeChords)
        if (chord.triggerNote >= 0 && (channel < 0 || chord.channel == channel))
            stopChord (chord, output, samplePosition);
}

//==============================================================================
void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() <= 0)
        return; // Leave UI FIFO and scheduler untouched until time advances.
    midiOverflow = keyboardOverflow.exchange (false, std::memory_order_relaxed);

    // Chord Settings Matrix. Relaxed ordering is enough for independent
    // scalars: these values are consumed by the MIDI math below and do not
    // synchronise any other memory.
    const auto chordTypeIndex = juce::roundToInt (typeParam->load (std::memory_order_relaxed));
    const auto complexityIndex = juce::roundToInt (complexityParam->load (std::memory_order_relaxed));
    const auto voicingIndex = juce::roundToInt (voicingParam->load (std::memory_order_relaxed));
    const auto pitchOffsetSemitones = juce::roundToInt (pitchOffsetParam->load (std::memory_order_relaxed));

    // Global interaction engine snapshots.
    const auto powerOn = powerParam->load (std::memory_order_relaxed) > 0.5f;
    const auto spreadValue = juce::jlimit (0.0f, 100.0f,
        spreadParam->load (std::memory_order_relaxed));
    const auto spreadModeIndex = juce::jlimit (0, 1,
        juce::roundToInt (spreadModeParam->load (std::memory_order_relaxed)));
    const auto humanizeValue = juce::jlimit (0.0f, 100.0f,
        humanizeParam->load (std::memory_order_relaxed));
    const auto noteOffModeIndex = juce::jlimit (0, 1,
        juce::roundToInt (noteOffModeParam->load (std::memory_order_relaxed)));
    const auto strumValue = juce::jlimit (0.0f, 100.0f,
        strumParam->load (std::memory_order_relaxed));
    const auto strumModeIndex = juce::jlimit (0, 1,
        juce::roundToInt (strumModeParam->load (std::memory_order_relaxed)));
    const auto strumTimingModeIndex = juce::jlimit (0, 1,
        juce::roundToInt (strumTimingModeParam->load (std::memory_order_relaxed)));
    const auto strumDivisionIndex = juce::jlimit (
        0, boilerplate::dsp::strum::numDivisions - 1,
        juce::roundToInt (strumDivisionParam->load (std::memory_order_relaxed)));
    const auto strumFallbackBpm = static_cast<double> (juce::jlimit (
        boilerplate::parameters::strumFallbackBpmMin,
        boilerplate::parameters::strumFallbackBpmMax,
        juce::roundToInt (strumFallbackBpmParam->load (std::memory_order_relaxed))));
    const auto strumPatternIndex = juce::jlimit (
        0, boilerplate::dsp::strum::numPatterns - 1,
        juce::roundToInt (strumPatternParam->load (std::memory_order_relaxed)));

    const auto gainDecibels = gainDbParameter->load (std::memory_order_relaxed);
    const auto bypassed = bypassParameter->load (std::memory_order_relaxed) > 0.5f
                       || ! powerOn;

    const auto chordType = juce::jlimit (0, chordTypeCount - 1, chordTypeIndex);
    const auto complexity = juce::jlimit (0, complexityCount - 1, complexityIndex);
    const auto voicing = juce::jlimit (0, 2, voicingIndex);
    const auto pitchOffset = juce::jlimit (boilerplate::parameters::pitchOffsetMin,
                                           boilerplate::parameters::pitchOffsetMax,
                                           pitchOffsetSemitones);

    const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;

    currentBlockSize = buffer.getNumSamples();
    currentNoteOffMode = noteOffModeIndex;
    currentSpreadMode = spreadModeIndex;
    currentStrumMode = strumModeIndex;
    currentStrumPattern = strumPatternIndex;
    currentSpreadAmount = spreadValue / 100.0f;
    currentHumanizeAmount = humanizeValue / 100.0f;
    currentBypassState = bypassed;
    currentVoicingStyle = juce::jlimit (0, 3, juce::roundToInt (voicingStyleParam->load()));
    currentScaleRoot = juce::jlimit (0, 11, juce::roundToInt (scaleRootParam->load()));
    currentScaleMode = juce::jlimit (0, 2, juce::roundToInt (scaleModeParam->load()));
    currentHumanizeMaxSamples = humanizeTimingParam->load() > 0.5f
        ? juce::roundToInt (currentHumanizeAmount * 0.010 * sampleRate) : 0;

    // Read the host playhead once for this block when synced timing needs it.
    // The pointer and position object are deliberately not retained: JUCE only
    // guarantees their validity inside processBlock. A stopped transport is
    // fine; manual notes still play.
    const auto hostBpm = strumTimingModeIndex == 1
        ? readHostTempoBpm (getPlayHead()) : 0.0;
    const auto effectiveBpm = hostBpm > 0.0 ? hostBpm : strumFallbackBpm;

    hostTempoActive.store (hostBpm > 0.0, std::memory_order_relaxed);
    effectiveStrumTempoBpm.store (static_cast<float> (effectiveBpm),
                                  std::memory_order_relaxed);

    currentStrumStepSamples = boilerplate::dsp::strum::stepSamples (
        strumTimingModeIndex == 1, sampleRate, strumValue,
        effectiveBpm, strumDivisionIndex);
    currentStaggerStepSamples = currentStrumStepSamples > 0
        ? currentStrumStepSamples
        : juce::roundToInt (minStaggerSecondsPerStep * sampleRate);

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Outputs without a matching input may contain garbage; clear them.
    for (auto channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

   #if JucePlugin_IsSynth
    // Instrument extension point: render preallocated voices into the cleared
    // output here, consuming MIDI at each event's samplePosition. Until a product
    // adds its engine this scaffold intentionally outputs silence.
   #endif

    // Output Gain scales the velocity of every generated chord note. The audio
    // buffer stays a transparent pass-through.
    const auto velocityGain = bypassed
        ? 1.0f
        : juce::Decibels::decibelsToGain (gainDecibels, -60.0f);

    // Move on-screen keyboard events across the lock-free FIFO. The buffer is
    // preallocated, so this never allocates in the audio callback.
    keyboardEventBuffer.clear();
    drainKeyboardEvents();

    midiOutputBuffer.clear();
    if (bypassed && ! wasBypassed)
        flushGeneratedNotes (midiOutputBuffer, 0);
    wasBypassed = bypassed;

    auto processInput = [&] (const juce::MidiMessageMetadata& metadata)
    {
        const auto* data = metadata.data;
        const auto size = metadata.numBytes;
        const auto position = juce::jlimit (0, currentBlockSize - 1, metadata.samplePosition);
        // Events due at this timestamp precede the incoming event. Thus a
        // note-off only cancels future tones, never tones already due.
        processPendingNotes (midiOutputBuffer, position + 1);
        updateInputNoteMask (data, size);
        if (size == 3 && data[1] < 128 && data[2] < 128)
        {
            const auto status = data[0] & 0xf0;
            const auto channel = data[0] & 0x0f;
            const auto note = static_cast<int> (data[1]);
            const auto value = static_cast<int> (data[2]);
            auto& rawHeld = passThroughNotes[channel][note];
            if (status == 0x90 && value > 0)
            {
                if (rawHeld)
                {
                    rawHeld = false;
                    if (! isNoteActive (channel, note))
                        appendMidi (midiOutputBuffer, juce::MidiMessage::noteOff (channel + 1, note), position);
                }
                if (bypassed)
                {
                    rawHeld = true;
                    appendMidi (midiOutputBuffer, data, size, position);
                }
                else
                {
                    const auto velocity = static_cast<juce::uint8> (juce::jlimit (1, 127,
                        juce::roundToInt (static_cast<float> (value) * velocityGain)));
                    startChord (note, channel, velocity, chordType, complexity, voicing,
                                pitchOffset, midiOutputBuffer, position);
                }
                return;
            }
            if (status == 0x80 || (status == 0x90 && value == 0))
            {
                if (rawHeld)
                {
                    rawHeld = false;
                    if (! isNoteActive (channel, note))
                        appendMidi (midiOutputBuffer, data, size, position);
                    return;
                }
                const auto index = findActiveChord (note, channel);
                if (index >= 0)
                {
                    stopChord (activeChords[index], midiOutputBuffer, position);
                    return;
                }
                if (isNoteActive (channel, note))
                    return;
            }
            if (status == 0xb0 && (note == 120 || note == 123))
            {
                flushGeneratedNotes (midiOutputBuffer, position, channel);
                std::memset (passThroughNotes[channel], 0, sizeof (passThroughNotes[channel]));
            }
        }
        appendMidi (midiOutputBuffer, data, size, position);
    };

    // UI events have timestamp zero. Process these before the sorted host
    // stream so ownership and pending events always move forwards in time.
    for (const auto metadata : keyboardEventBuffer)
        processInput (metadata);
    int inputs = 0;
    for (const auto metadata : midiMessages)
    {
        if (++inputs > maxInputEventsPerBlock || midiOverflow)
        {
            midiOverflow = true;
            break;
        }
        processInput (metadata);
    }
    processPendingNotes (midiOutputBuffer, currentBlockSize);
    if (midiOverflow)
    {
        // Bounded overload policy: abandon the block and silence every channel
        // rather than drop a release and leave the destination stuck.
        midiOutputBuffer.clear();
        resetChordState();
        for (int channel = 1; channel <= 16; ++channel)
        {
            appendMidi (midiOutputBuffer, juce::MidiMessage::allSoundOff (channel), 0);
            appendMidi (midiOutputBuffer, juce::MidiMessage::allNotesOff (channel), 0);
        }
    }

    totalSamplesProcessed += static_cast<juce::int64> (currentBlockSize);

    // Preserve our reservation. The host owns this destination; its JUCE
    // buffer may grow here if the host did not reserve enough for expansion.
    midiMessages.clear();
    midiMessages.addEvents (midiOutputBuffer, 0, currentBlockSize, 0);

    // Publish the notes that survive in the final, chord-expanded buffer. The
    // virtual keyboard reads this lock-free mask on the message thread, so the
    // display always mirrors the plugin's real MIDI output rather than the raw
    // input that produced it.
    for (const auto metadata : midiMessages)
        updateOutputNoteMask (metadata.data, metadata.numBytes);
}

//==============================================================================
bool PluginProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
juce::ValueTree PluginProcessor::createKeyRecipeState() const
{
    juce::ValueTree root (keyRecipesStateTag);

    for (int note = 0; note < 128; ++note)
    {
        const auto packed = keyRecipes[note].load (std::memory_order_acquire);

        if ((packed & keyRecipeOverrideBit) == 0)
            continue;

        const auto recipe = unpackKeyRecipe (packed);

        juce::ValueTree key (keyRecipeStateTag);
        key.setProperty ("note", note, nullptr);
        key.setProperty ("type", recipe.chordType, nullptr);
        key.setProperty ("complexity", recipe.complexity, nullptr);
        key.setProperty ("voicing", recipe.voicing, nullptr);
        key.setProperty ("pitch", recipe.pitchOffset, nullptr);
        root.appendChild (key, nullptr);
    }

    for (int pc = 0; pc < 12; ++pc)
    {
        if (! hasPitchClassRecipe (pc))
            continue;
        const auto recipe = getPitchClassRecipe (pc);
        juce::ValueTree key (keyRecipeStateTag);
        key.setProperty ("pitchClass", pc, nullptr);
        key.setProperty ("type", recipe.chordType, nullptr);
        key.setProperty ("complexity", recipe.complexity, nullptr);
        key.setProperty ("voicing", recipe.voicing, nullptr);
        key.setProperty ("pitch", recipe.pitchOffset, nullptr);
        root.appendChild (key, nullptr);
    }
    return root;
}

void PluginProcessor::restoreKeyRecipeState (const juce::ValueTree& state)
{
    // A state without a KeyRecipes child is an older session or a preset that
    // intentionally has no per-key overrides: clear the whole map first.
    for (auto& recipe : keyRecipes)
        recipe.store (0, std::memory_order_release);

    for (auto& recipe : pitchClassRecipes)
        recipe.store (0, std::memory_order_release);
    const auto root = state.getChildWithName (keyRecipesStateTag);

    if (! root.isValid())
        return;

    for (int i = 0; i < root.getNumChildren(); ++i)
    {
        const auto key = root.getChild (i);

        if (! key.hasType (keyRecipeStateTag))
            continue;

        const auto note = static_cast<int> (key.getProperty ("note", -1));

        const auto pc = static_cast<int> (key.getProperty ("pitchClass", -1));
        if (! juce::isPositiveAndBelow (note, 128) && ! juce::isPositiveAndBelow (pc, 12))
            continue;

        ChordRecipe recipe;
        recipe.chordType = static_cast<int> (key.getProperty ("type", 0));
        recipe.complexity = static_cast<int> (key.getProperty ("complexity", 0));
        recipe.voicing = static_cast<int> (key.getProperty ("voicing", 1));
        recipe.pitchOffset = static_cast<int> (key.getProperty ("pitch", 0));
        if (juce::isPositiveAndBelow (pc, 12))
            setPitchClassRecipe (pc, recipe);
        else
            setKeyRecipe (note, recipe);
    }
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    if (!state.isValid())
        return;

    const auto existing = state.getChildWithName (keyRecipesStateTag);

    if (existing.isValid())
        state.removeChild (existing, nullptr);

    state.setProperty ("schemaVersion", 2, nullptr);
    state.appendChild (createKeyRecipeState(), nullptr);

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    auto state = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes));

    if (state.isValid() && state.hasType (apvts.state.getType()))
    {
        // Missing new controls identify older sessions or presets. Explicit
        // child values preserve the released interpretation for the v2 controls
        // and give tempo-synced strum its Free/sixteenth/120 defaults so old
        // sessions sound exactly as they did before the timing controls existed.
        const auto addMissingValue = [&state] (const char* id, float value)
        {
            for (const auto& child : state)
                if (child.getProperty ("id").toString() == id)
                    return;

            juce::ValueTree child ("PARAM");
            child.setProperty ("id", id, nullptr);
            child.setProperty ("value", value, nullptr);
            state.appendChild (child, nullptr);
        };

        addMissingValue (boilerplate::parameters::voicingStyle, 0.0f);
        addMissingValue (boilerplate::parameters::humanizeTiming, 0.0f);
        addMissingValue (boilerplate::parameters::scaleRoot, 0.0f);
        addMissingValue (boilerplate::parameters::scaleMode, 0.0f);
        addMissingValue (boilerplate::parameters::strumTimingMode, 0.0f);
        addMissingValue (boilerplate::parameters::strumDivision,
                         static_cast<float> (boilerplate::dsp::strum::defaultDivisionIndex));
        addMissingValue (boilerplate::parameters::strumFallbackBpm,
                         static_cast<float> (boilerplate::parameters::strumFallbackBpmDefault));
        addMissingValue (boilerplate::parameters::strumPattern, 0.0f);

        apvts.replaceState (state);
        restoreKeyRecipeState (state);
    }
}

//==============================================================================
// JUCE's format wrappers call this global factory.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
