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

    jassert (gainDbParameter != nullptr);
    jassert (bypassParameter != nullptr);
    jassert (bypassParameterObject != nullptr);
    jassert (typeParam != nullptr);
    jassert (complexityParam != nullptr);
    jassert (voicingParam != nullptr);
    jassert (pitchOffsetParam != nullptr);

    for (auto& recipe : keyRecipes)
        recipe.store (0, std::memory_order_relaxed);

    for (auto& word : activeInputNotes)
        word.store (0, std::memory_order_relaxed);

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

    std::memset (noteRefCounts, 0, sizeof (noteRefCounts));
    clearInputNotes();
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

    const auto& shape = chordShapes[chordType][complexity];
    const auto numTones = juce::jlimit (0, maxChordTones, shape.numTones);

    if (numTones == 0)
        return 0;

    // Per-key assignment: the trigger key is the chord root, so different keys
    // and octaves generate different chords from the same matrix state.
    int base = triggerNote + pitchOffset + voicingOctaveOffset (voicing);

    // Keep the whole shape inside the MIDI range by moving it in octaves. This
    // preserves the intervals instead of folding individual notes into a
    // different inversion.
    const auto highestInterval = shape.intervals[numTones - 1];

    while (base < 0)
        base += 12;

    while (base + highestInterval > 127)
        base -= 12;

    if (base < 0 || base > 127)
        return 0;

    int count = 0;

    for (int i = 0; i < numTones; ++i)
    {
        const auto note = base + shape.intervals[i];

        if (note >= 0 && note <= 127)
            notesOut[count++] = static_cast<juce::uint8> (note);
    }

    return count;
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

    juce::uint8 notes[maxChordTones] {};
    const auto numNotes = buildChordNotes (triggerNote, chordType, complexity,
                                           voicing, pitchOffset, notes);

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

    slot->triggerNote = triggerNote;
    slot->channel = channel;
    slot->numNotes = numNotes;

    for (int i = 0; i < numNotes; ++i)
        slot->notes[i] = notes[i];

    for (int i = 0; i < numNotes; ++i)
    {
        const auto note = notes[i];
        auto& refCount = noteRefCounts[channel][note];

        // Reference counting means a note shared by two held chords is only
        // switched off when the last chord that owns it is released.
        if (refCount == 0)
            output.addEvent (juce::MidiMessage::noteOn (channel + 1, note, velocity),
                             samplePosition);

        if (refCount < 255)
            ++refCount;
    }

    return true;
}

void PluginProcessor::stopChord (ActiveChord& chord, juce::MidiBuffer& output,
                                 int samplePosition) noexcept
{
    if (! juce::isPositiveAndBelow (chord.channel, midiChannels))
    {
        chord = {};
        return;
    }

    for (int i = 0; i < chord.numNotes; ++i)
    {
        const auto note = chord.notes[i];
        auto& refCount = noteRefCounts[chord.channel][note];

        if (refCount > 0)
        {
            --refCount;

            if (refCount == 0)
                output.addEvent (juce::MidiMessage::noteOff (chord.channel + 1, note),
                                 samplePosition);
        }
    }

    chord = {};
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

    const auto gainDecibels = gainDbParameter->load (std::memory_order_relaxed);
    const auto bypassed = bypassParameter->load (std::memory_order_relaxed) > 0.5f;

    const auto chordType = juce::jlimit (0, chordTypeCount - 1, chordTypeIndex);
    const auto complexity = juce::jlimit (0, complexityCount - 1, complexityIndex);
    const auto voicing = juce::jlimit (0, 2, voicingIndex);
    const auto pitchOffset = juce::jlimit (boilerplate::parameters::pitchOffsetMin,
                                           boilerplate::parameters::pitchOffsetMax,
                                           pitchOffsetSemitones);

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

    midiMessages.swapWith (midiSwapBuffer);
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
