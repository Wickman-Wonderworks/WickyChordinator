#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "State/ParameterIDs.h"
#include "State/ParameterLayout.h"

#include <cstring>

namespace
{
    constexpr int kMaxChordTones = 8; // Matches PluginProcessor::maxChordTones.
    constexpr int chordTypeCount = 4;
    constexpr int complexityCount = 11;
    constexpr float maxStrumSecondsPerStep = 0.060f;
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
    return 0.0;
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

void PluginProcessor::setInputNoteActive (const int midiNote, const bool active) noexcept
{
    if (! juce::isPositiveAndBelow (midiNote, 128))
        return;

    auto& word = activeInputNotes[midiNote >> 6];
    const auto bit = static_cast<juce::uint64> (1) << (midiNote & 63);

    if (active)
        word.fetch_or (bit, std::memory_order_relaxed);
    else
        word.fetch_and (~bit, std::memory_order_relaxed);
}

void PluginProcessor::clearInputNotes() noexcept
{
    for (auto& word : activeInputNotes)
        word.store (0, std::memory_order_relaxed);
}

void PluginProcessor::setOutputNoteActive (const int midiNote, const bool active) noexcept
{
    if (! juce::isPositiveAndBelow (midiNote, 128))
        return;

    auto& word = activeOutputNotes[midiNote >> 6];
    const auto bit = static_cast<juce::uint64> (1) << (midiNote & 63);

    if (active)
        word.fetch_or (bit, std::memory_order_relaxed);
    else
        word.fetch_and (~bit, std::memory_order_relaxed);
}

void PluginProcessor::clearOutputNotes() noexcept
{
    for (auto& word : activeOutputNotes)
        word.store (0, std::memory_order_relaxed);
}

void PluginProcessor::updateInputNoteMask (const juce::uint8* data,
                                           const int numBytes) noexcept
{
    if (data == nullptr || numBytes != 3)
        return;

    const auto status = data[0] & 0xf0;
    const auto note = static_cast<int> (data[1]);
    const auto value = static_cast<int> (data[2]);

    if (status == 0x90 && value > 0)
        setInputNoteActive (note, true);
    else if (status == 0x80 || (status == 0x90 && value == 0))
        setInputNoteActive (note, false);
    else if (status == 0xB0 && (data[1] == 120 || data[1] == 123))
        clearInputNotes();
}

void PluginProcessor::updateOutputNoteMask (const juce::uint8* data,
                                            const int numBytes) noexcept
{
    if (data == nullptr || numBytes != 3)
        return;

    const auto status = data[0] & 0xf0;
    const auto note = static_cast<int> (data[1]);
    const auto value = static_cast<int> (data[2]);

    if (status == 0x90 && value > 0)
        setOutputNoteActive (note, true);
    else if (status == 0x80 || (status == 0x90 && value == 0))
        setOutputNoteActive (note, false);
    else if (status == 0xB0 && (data[1] == 120 || data[1] == 123))
        clearOutputNotes();
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
    juce::ignoreUnused (sampleRate);

    // The copy-then-swap pass reuses this buffer every block. Reserving here
    // keeps the audio callback allocation-free in steady state.
    midiSwapBuffer.clear();
    midiSwapBuffer.ensureSize (static_cast<size_t> (juce::jmax (65536, samplesPerBlock * 128)));

    keyboardEventBuffer.clear();
    keyboardEventBuffer.ensureSize (keyboardEventBufferBytes);
    keyboardEventFifo.reset();

    // Re-seed the relaxed PRNG at preparation time. The audio thread only ever
    // performs relaxed loads/stores on this atomic, so no lock or allocation is
    // introduced into processBlock.
    humanizeSeed.store (static_cast<juce::uint32> (juce::Random::getSystemRandom().nextInt()),
                        std::memory_order_relaxed);

    totalSamplesProcessed = 0;
    resetChordState();
    wasBypassed = bypassParameter != nullptr && bypassParameter->load() > 0.5f;
}

void PluginProcessor::releaseResources()
{
    midiSwapBuffer.clear();
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

//==============================================================================
int PluginProcessor::buildChordNotes (int triggerNote, int chordType, int complexity,
                                      int voicing, int pitchOffset,
                                      float spreadAmount, int spreadMode,
                                      juce::uint8* notesOut) const noexcept
{
    if (notesOut == nullptr)
        return 0;

    // Per-key assignment: an explicit override wins; otherwise the global UI
    // matrix values passed in from processBlock are used.
    if (juce::isPositiveAndBelow (triggerNote, 128))
    {
        const auto packed = keyRecipes[triggerNote].load (std::memory_order_acquire);

        if ((packed & keyRecipeOverrideBit) != 0)
        {
            const auto recipe = unpackKeyRecipe (packed);
            chordType = recipe.chordType;
            complexity = recipe.complexity;
            voicing = recipe.voicing;
            pitchOffset = recipe.pitchOffset;
        }
    }

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
    int base = triggerNote + pitchOffset + voicingOctaveOffset (voicing);

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

    for (int i = 0; i < numTones; ++i)
    {
        const auto note = base + shape.intervals[i] + spreadOffsets[i];

        if (note >= 0 && note <= 127)
            notesOut[count++] = static_cast<juce::uint8> (note);
    }

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

    if (refCount == 0)
        output.addEvent (juce::MidiMessage::noteOn (channel + 1, note, velocity),
                         samplePosition);

    if (refCount < 255)
        ++refCount;
}

void PluginProcessor::scheduleNoteOn (int chordIndex, int noteIndex, int note,
                                      int channel, juce::uint8 velocity,
                                      int targetSample,
                                      juce::MidiBuffer& output) noexcept
{
    targetSample = juce::jmax (0, targetSample);

    // With no strum, preserve the original immediate path exactly. This keeps
    // sample positions and ref-counting identical for existing presets.
    if (currentStrumStepSamples <= 0 || targetSample < currentBlockSize)
    {
        if (currentStrumStepSamples <= 0)
        {
            emitNoteOn (chordIndex, noteIndex, note, channel, velocity,
                        juce::jmin (targetSample, juce::jmax (0, currentBlockSize - 1)),
                        output);
            return;
        }
    }

    // Strummed notes are always queued, even when their target lies inside the
    // current block. This lets a fast note-off in the same block cancel the
    // remaining notes before any premature note-on reaches the host.
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
                juce::jmax (0, currentBlockSize - 1), output);
}

void PluginProcessor::scheduleNoteOff (int channel, int note, int targetSample,
                                       juce::MidiBuffer& output) noexcept
{
    if (! juce::isPositiveAndBelow (channel, midiChannels)
        || ! juce::isPositiveAndBelow (note, 128))
        return;

    // Release ownership immediately, but defer the wire-level note-off. If
    // another chord still owns the note, no event is needed at all.
    auto& refCount = noteRefCounts[channel][note];

    if (refCount > 0)
        --refCount;

    if (refCount != 0)
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

    // Queue exhausted: send the note-off right away rather than leak a note.
    output.addEvent (juce::MidiMessage::noteOff (channel + 1, note),
                     juce::jmax (0, currentBlockSize - 1));
}

void PluginProcessor::processPendingNotes (juce::MidiBuffer& output,
                                           int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const auto blockStart = totalSamplesProcessed;
    const auto blockEnd = blockStart + static_cast<juce::int64> (numSamples);

    for (auto& pending : pendingNotes)
    {
        if (! pending.active || pending.dueSample >= blockEnd)
            continue;

        const auto position = static_cast<int> (
            juce::jlimit<juce::int64> (0, static_cast<juce::int64> (numSamples - 1),
                                       pending.dueSample - blockStart));

        if (pending.isNoteOn)
        {
            // Bypass may have engaged after this note was scheduled.
            if (! currentBypassState)
                emitNoteOn (pending.chordIndex, pending.noteIndex, pending.note,
                            pending.channel, pending.velocity, position, output);
        }
        else
        {
            // The ref-count was already decremented when the release was
            // scheduled. Only emit the wire note-off if nobody re-took the note
            // in the meantime.
            if (noteRefCounts[pending.channel][pending.note] == 0)
                output.addEvent (
                    juce::MidiMessage::noteOff (pending.channel + 1, pending.note),
                    position);
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

    juce::uint8 notes[maxChordTones] {};
    const auto numNotes = buildChordNotes (triggerNote, chordType, complexity,
                                           voicing, pitchOffset,
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

    // Preserve the exact evaluated recipe for the editor's symbol readout.
    // Per-key overrides win over the global matrix exactly as they do when
    // buildChordNotes constructed the notes above.
    ChordRecipe evaluatedRecipe;
    evaluatedRecipe.chordType = chordType;
    evaluatedRecipe.complexity = complexity;
    evaluatedRecipe.voicing = voicing;
    evaluatedRecipe.pitchOffset = pitchOffset;

    if (juce::isPositiveAndBelow (triggerNote, 128))
    {
        const auto packed = keyRecipes[triggerNote].load (std::memory_order_acquire);

        if ((packed & keyRecipeOverrideBit) != 0)
            evaluatedRecipe = unpackKeyRecipe (packed);
    }

    lastTriggeredRecipe.store (packKeyRecipe (evaluatedRecipe),
                               std::memory_order_relaxed);

    // Strum Up plays low-to-high; Strum Down plays high-to-low. The per-step
    // distance is derived from the current sample rate inside processBlock.
    const auto down = currentStrumMode == 1;

    for (int order = 0; order < numNotes; ++order)
    {
        const auto noteIndex = down ? (numNotes - 1 - order) : order;
        const auto delaySamples = currentStrumStepSamples * order;

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

                if (refCount == 0)
                    output.addEvent (juce::MidiMessage::noteOff (channel + 1, note),
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
    currentSpreadAmount = spreadValue / 100.0f;
    currentHumanizeAmount = humanizeValue / 100.0f;
    currentBypassState = bypassed;

    currentStrumStepSamples = juce::roundToInt (
        (strumValue / 100.0f) * maxStrumSecondsPerStep * sampleRate);
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

    // UI-only mirror of the held input notes. It never influences the audio
    // path; the editor polls it to highlight keys on the virtual keyboard.
    for (const auto metadata : midiMessages)
        updateInputNoteMask (metadata.data, metadata.numBytes);

    for (const auto metadata : keyboardEventBuffer)
        updateInputNoteMask (metadata.data, metadata.numBytes);

    // Copy-then-swap: the source buffers are never mutated while they are
    // being iterated, so no iterator can be invalidated. Three-byte MIDI
    // messages are stored inline by juce::MidiMessage, so the per-event path
    // does not allocate.
    midiSwapBuffer.clear();

    // Retire every sounding chord on the bypass edge so no generated note can
    // be orphaned while the input is passed through.
    if (bypassed && ! wasBypassed)
        releaseAllChords (midiSwapBuffer, 0);

    wasBypassed = bypassed;

    if (bypassed)
    {
        for (int sourceIndex = 0; sourceIndex < 2; ++sourceIndex)
        {
            const juce::MidiBuffer& source = sourceIndex == 0 ? midiMessages
                                                              : keyboardEventBuffer;

            for (const auto metadata : source)
                midiSwapBuffer.addEvent (metadata.data, metadata.numBytes,
                                         metadata.samplePosition);
        }
    }
    else
    {
        for (int sourceIndex = 0; sourceIndex < 2; ++sourceIndex)
        {
            const juce::MidiBuffer& source = sourceIndex == 0 ? midiMessages
                                                              : keyboardEventBuffer;

            for (const auto metadata : source)
            {
                const auto* data = metadata.data;
                const auto numBytes = metadata.numBytes;
                const auto samplePosition = metadata.samplePosition;

                if (numBytes == 3)
                {
                    const auto status = data[0] & 0xf0;
                    const auto channel = static_cast<int> (data[0] & 0x0f);
                    const auto note = static_cast<int> (data[1]);
                    const auto value = static_cast<int> (data[2]);

                    if (status == 0x90 && value > 0)
                    {
                        const auto scaledVelocity = static_cast<juce::uint8> (
                            juce::jlimit (1, 127,
                                juce::roundToInt (static_cast<float> (value) * velocityGain)));

                        if (! startChord (note, channel, scaledVelocity,
                                          chordType, complexity, voicing, pitchOffset,
                                          midiSwapBuffer, samplePosition))
                        {
                            // Safety fallback for an empty/invalid shape: keep the
                            // original note instead of swallowing the key.
                            midiSwapBuffer.addEvent (
                                juce::MidiMessage::noteOn (channel + 1, note, scaledVelocity),
                                samplePosition);
                        }

                        continue;
                    }

                    if (status == 0x80 || (status == 0x90 && value == 0))
                    {
                        const auto chordIndex = findActiveChord (note, channel);

                        if (chordIndex >= 0)
                        {
                            stopChord (activeChords[chordIndex], midiSwapBuffer, samplePosition);
                            continue;
                        }

                        // A note-off aimed at a generated note must not cut a note
                        // that another chord still owns.
                        if (isNoteActive (channel, note))
                            continue;

                        // Otherwise it belongs to a pass-through/bypassed note.
                    }

                    if (status == 0xB0 && (data[1] == 120 || data[1] == 123))
                        releaseAllChords (midiSwapBuffer, samplePosition, channel);
                }

                midiSwapBuffer.addEvent (data, numBytes, samplePosition);
            }
        }
    }

    // Dispatch strummed note-ons and staggered note-offs whose sample time has
    // arrived. This runs after all incoming events so a fast release in the
    // same block can cancel pending note-ons before they reach the host.
    processPendingNotes (midiSwapBuffer, currentBlockSize);

    totalSamplesProcessed += static_cast<juce::int64> (currentBlockSize);

    midiMessages.swapWith (midiSwapBuffer);

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

    return root;
}

void PluginProcessor::restoreKeyRecipeState (const juce::ValueTree& state)
{
    // A state without a KeyRecipes child is an older session or a preset that
    // intentionally has no per-key overrides: clear the whole map first.
    for (auto& recipe : keyRecipes)
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

        if (! juce::isPositiveAndBelow (note, 128))
            continue;

        ChordRecipe recipe;
        recipe.chordType = static_cast<int> (key.getProperty ("type", 0));
        recipe.complexity = static_cast<int> (key.getProperty ("complexity", 0));
        recipe.voicing = static_cast<int> (key.getProperty ("voicing", 1));
        recipe.pitchOffset = static_cast<int> (key.getProperty ("pitch", 0));
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

    state.appendChild (createKeyRecipeState(), nullptr);

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto state = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes));

    if (state.isValid() && state.hasType (apvts.state.getType()))
    {
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
