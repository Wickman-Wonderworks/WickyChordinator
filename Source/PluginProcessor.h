#pragma once

#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class PluginProcessor final : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    using AudioProcessor::processBlock;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==========================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==========================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

    //==========================================================================
    /**
        Display state for the editor's virtual keyboard.

        This state is owned by the processor but is only ever mutated from the
        message thread. It is continuously reconciled with the notes that the
        audio thread published from the final, chord-expanded MIDI output so the
        keyboard mirrors what the plugin actually emits.
    */
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

    /** Lock-free snapshot of the raw input notes currently held by the host. */
    juce::uint64 getActiveInputNotes (int wordIndex) const noexcept;

    /**
        Lock-free snapshot of the notes in the final processed MIDI output.

        processBlock refreshes this mask from the chord-expanded buffer that is
        handed back to the host. The editor polls it to drive the keyboard, so
        the display can never drift from the real output.
    */
    juce::uint64 getActiveOutputNotes (int wordIndex) const noexcept;

    /** Queues a virtual-keyboard note for the next processBlock without locking. */
    void queueKeyboardNote (bool noteOn, int midiNote, float velocity) noexcept;

    /** Last trigger key that started a chord (message-thread safe). */
    int getLastTriggeredChordNote() const noexcept;

    /** Number of currently held/generated chords (message-thread safe). */
    int getNumSoundingChords() const noexcept;

    //==========================================================================
    /** One chord assignment for a single MIDI note. */
    struct ChordRecipe
    {
        int chordType = 0;   // 0..3
        int complexity = 0;  // 0..10
        int voicing = 1;     // 0..2 (High/Root/Low)
        int pitchOffset = 0; // -12..+12
    };

    /** Evaluated recipe that generated the last chord (message-thread safe). */
    ChordRecipe getLastTriggeredChordRecipe() const noexcept;

    /** Message-thread API for the per-key recipe map. Safe while audio runs. */
    void setKeyRecipe (int midiNote, ChordRecipe recipe) noexcept;
    void clearKeyRecipe (int midiNote) noexcept;
    bool hasKeyRecipe (int midiNote) const noexcept;
    /** Returns the override, or the current global matrix values if unassigned. */
    ChordRecipe getKeyRecipe (int midiNote) const noexcept;
    void setPitchClassRecipe (int pitchClass, ChordRecipe) noexcept;
    void clearPitchClassRecipe (int pitchClass) noexcept;
    bool hasPitchClassRecipe (int pitchClass) const noexcept;
    ChordRecipe getPitchClassRecipe (int pitchClass) const noexcept;

private:
    static constexpr int midiOutputBytes = 65536;
    static constexpr int maxInputEventsPerBlock = 4096;
    friend class PluginProcessorTestAccess;
    bool midiOverflow = false;
    std::atomic<bool> keyboardOverflow { false };
    bool appendMidi (juce::MidiBuffer&, const juce::uint8*, int, int) noexcept;
    bool appendMidi (juce::MidiBuffer&, const juce::MidiMessage&, int) noexcept;
    void flushGeneratedNotes (juce::MidiBuffer&, int, int channel = -1) noexcept;
    bool passThroughNotes[16][128] {};
    bool inputNotesByChannel[16][128] {};
    bool outputNotesByChannel[16][128] {};
    void updateNoteMask (const juce::uint8*, int, bool (&)[16][128],
                         std::atomic<juce::uint64> (&)[2]) noexcept;

    // Display mirror of the on-screen keyboard. The audio thread never touches
    // this object; processBlock publishes the final output note mask below and
    // the editor's timer applies it on the message thread.
    juce::MidiKeyboardState keyboardState;

    // Lock-free SPSC bridge from the editor's keyboard to the audio callback.
    struct KeyboardEvent
    {
        juce::uint8 data[3] {};
    };

    static constexpr int keyboardEventCapacity = 256;
    static constexpr int keyboardEventBufferBytes = 8192;

    juce::AbstractFifo keyboardEventFifo { keyboardEventCapacity };
    KeyboardEvent keyboardEvents[keyboardEventCapacity] {};
    juce::MidiBuffer keyboardEventBuffer;

    // Two 64-bit words: bit n of word 0/1 means "input note n is held".
    std::atomic<juce::uint64> activeInputNotes[2] {};

    // Two 64-bit words describing the notes present in the final, processed
    // MIDI output. Written only by the audio thread inside processBlock and
    // read by the editor to drive the virtual keyboard display.
    std::atomic<juce::uint64> activeOutputNotes[2] {};

    void drainKeyboardEvents() noexcept;
    void updateInputNoteMask (const juce::uint8* data, int numBytes) noexcept;
    void updateOutputNoteMask (const juce::uint8* data, int numBytes) noexcept;
    void clearInputNotes() noexcept;
    void clearOutputNotes() noexcept;

    // Declared before the raw parameter pointers so that it outlives them.
    juce::AudioProcessorValueTreeState apvts;

    // Cached once after the APVTS is constructed; safe to read on the audio thread.
    std::atomic<float>* gainDbParameter = nullptr;
    std::atomic<float>* bypassParameter = nullptr;
    juce::AudioProcessorParameter* bypassParameterObject = nullptr;

    // Chord Settings Matrix. Raw atomics only: no APVTS or String lookup is
    // allowed on the audio thread.
    std::atomic<float>* typeParam = nullptr;
    std::atomic<float>* complexityParam = nullptr;
    std::atomic<float>* voicingParam = nullptr;
    std::atomic<float>* pitchOffsetParam = nullptr;

    // Global interaction engine. These pointers are cached alongside the chord
    // matrix and read with relaxed ordering inside processBlock.
    std::atomic<float>* powerParam = nullptr;
    std::atomic<float>* spreadParam = nullptr;
    std::atomic<float>* spreadModeParam = nullptr;
    std::atomic<float>* humanizeParam = nullptr;
    std::atomic<float>* noteOffModeParam = nullptr;
    std::atomic<float>* strumParam = nullptr;
    std::atomic<float>* strumModeParam = nullptr;
    std::atomic<float>* voicingStyleParam = nullptr;
    std::atomic<float>* humanizeTimingParam = nullptr;
    std::atomic<float>* scaleRootParam = nullptr;
    std::atomic<float>* scaleModeParam = nullptr;

    // Owned bounded output scratch. Never exchange its reservation with host storage.
    juce::MidiBuffer midiOutputBuffer;

    //==========================================================================
    // Per-key chord recipes. One packed, lock-free 32-bit atomic per MIDI note:
    // type (2 bits) | complexity (4) | voicing (2) | pitch+12 (5) | override
    // flag (bit 31). A zero value means "no override, use the global matrix".
    std::atomic<juce::uint32> keyRecipes[128] {};
    std::atomic<juce::uint32> pitchClassRecipes[12] {};

    static constexpr juce::uint32 keyRecipeOverrideBit = 1u << 31;

   #if defined(__cpp_lib_atomic_is_always_lock_free)
    static_assert (std::atomic<juce::uint64>::is_always_lock_free,
                   "Keyboard snapshots require lock-free 64-bit atomics");
    static_assert (std::atomic<juce::uint32>::is_always_lock_free,
                   "Per-key recipes require lock-free 32-bit atomics");
   #endif

    static juce::uint32 packKeyRecipe (const ChordRecipe& recipe) noexcept;
    static ChordRecipe unpackKeyRecipe (juce::uint32 packed) noexcept;

    juce::ValueTree createKeyRecipeState() const;
    void restoreKeyRecipeState (const juce::ValueTree& state);

    //==========================================================================
    // Chord engine state. Everything below is touched only by the audio thread
    // unless explicitly marked otherwise.
    static constexpr int maxChordTones = 8;
    static constexpr int maxActiveChords = 128;
    static constexpr int midiChannels = 16;

    struct ActiveChord
    {
        int triggerNote = -1; // -1 marks a free slot.
        int channel = 0;
        int numNotes = 0;
        juce::uint8 notes[maxChordTones] {};
        bool noteOnSent[maxChordTones] {};
    };

    // A note-on or note-off that could not be placed inside the current block.
    // The array is preallocated: no allocation is ever performed for strumming
    // or staggered releases.
    struct PendingNote
    {
        int chordIndex = -1; // -1 for a note-off (not owned by a chord slot).
        int noteIndex = 0;
        int channel = 0;
        int note = 0;
        juce::uint8 velocity = 0;
        juce::int64 dueSample = 0;
        bool isNoteOn = false;
        bool active = false;
    };

    static constexpr int maxPendingNotes = 512;

    ActiveChord activeChords[maxActiveChords];
    PendingNote pendingNotes[maxPendingNotes] {};
    juce::uint16 noteRefCounts[midiChannels][128] {};
    bool wasBypassed = false;

    // Snapshot of the block-local engine settings, refreshed at the start of
    // processBlock and consumed by the chord helpers below.
    int currentBlockSize = 0;
    int currentStrumStepSamples = 0;
    int currentStaggerStepSamples = 0;
    int currentNoteOffMode = 0;
    int currentSpreadMode = 0;
    int currentStrumMode = 0;
    float currentSpreadAmount = 0.0f;
    float currentHumanizeAmount = 0.0f;
    bool currentBypassState = false;
    int currentHumanizeMaxSamples = 0;
    int currentVoicingStyle = 0;
    int currentScaleRoot = 0;
    int currentScaleMode = 0;
    juce::uint8 previousVoicing[16][8] {};
    int previousVoicingCount[16] {};
    void applyVoicing (juce::uint8*, int, int, int) noexcept;

    // Monotonic sample clock for the pending strum/stagger scheduler. Only the
    // audio thread touches it.
    juce::int64 totalSamplesProcessed = 0;

    // Relaxed-atomic PRNG state. Only the audio thread advances it; the message
    // thread may re-seed it while the engine is stopped.
    std::atomic<juce::uint32> humanizeSeed { 0x9e3779b9u };

    // Message-thread-safe chord status for the editor's symbol readout.
    std::atomic<int> lastTriggeredChordNote { -1 };
    std::atomic<juce::uint32> lastTriggeredRecipe { 0 };
    std::atomic<int> soundingChordCount { 0 };

    void resetChordState() noexcept;

    int buildChordNotes (int triggerNote, int chordType, int complexity,
                         int voicing, int pitchOffset,
                         float spreadAmount, int spreadMode,
                         juce::uint8* notesOut) const noexcept;

    bool startChord (int triggerNote, int channel, juce::uint8 velocity,
                     int chordType, int complexity, int voicing, int pitchOffset,
                     juce::MidiBuffer& output, int samplePosition) noexcept;

    void stopChord (ActiveChord& chord, juce::MidiBuffer& output,
                    int samplePosition) noexcept;

    int findActiveChord (int triggerNote, int channel) const noexcept;
    bool isNoteActive (int channel, int note) const noexcept;
    void releaseAllChords (juce::MidiBuffer& output, int samplePosition,
                           int channel = -1) noexcept;

    //==========================================================================
    // Strum / humanize / staggered-release plumbing.
    void emitNoteOn (int chordIndex, int noteIndex, int note, int channel,
                     juce::uint8 velocity, int samplePosition,
                     juce::MidiBuffer& output) noexcept;

    void scheduleNoteOn (int chordIndex, int noteIndex, int note, int channel,
                         juce::uint8 velocity, int targetSample,
                         juce::MidiBuffer& output) noexcept;

    void scheduleNoteOff (int channel, int note, int targetSample,
                          juce::MidiBuffer& output) noexcept;

    void processPendingNotes (juce::MidiBuffer& output, int numSamples) noexcept;
    void cancelPendingNotesForChord (int chordIndex) noexcept;

    juce::uint32 nextRandom() noexcept;
    juce::uint8 humanizedVelocity (juce::uint8 velocity) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
