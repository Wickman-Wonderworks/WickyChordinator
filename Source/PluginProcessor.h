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
    /** Message-thread keyboard state used by the editor's virtual keyboard. */
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

    /** Lock-free snapshot of the notes currently held by the host. */
    juce::uint64 getActiveInputNotes (int wordIndex) const noexcept;

    /** Queues a virtual-keyboard note for the next processBlock without locking. */
    void queueKeyboardNote (bool noteOn, int midiNote, float velocity) noexcept;

    //==========================================================================
    /** One chord assignment for a single MIDI note. */
    struct ChordRecipe
    {
        int chordType = 0;   // 0..3
        int complexity = 0;  // 0..10
        int voicing = 1;     // 0..2 (High/Root/Low)
        int pitchOffset = 0; // -12..+12
    };

    /** Message-thread API for the per-key recipe map. Safe while audio runs. */
    void setKeyRecipe (int midiNote, ChordRecipe recipe) noexcept;
    void clearKeyRecipe (int midiNote) noexcept;
    bool hasKeyRecipe (int midiNote) const noexcept;
    /** Returns the override, or the current global matrix values if unassigned. */
    ChordRecipe getKeyRecipe (int midiNote) const noexcept;

private:
    // Message-thread view of the on-screen keyboard. The audio thread never
    // touches this object; note events cross over through the FIFO below.
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

    void drainKeyboardEvents() noexcept;
    void updateInputNoteMask (const juce::uint8* data, int numBytes) noexcept;
    void setInputNoteActive (int midiNote, bool active) noexcept;
    void clearInputNotes() noexcept;

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

    // Audio-thread scratch for the copy-then-swap MIDI pass. Preallocated in
    // prepareToPlay so processBlock never has to grow it.
    juce::MidiBuffer midiSwapBuffer;

    //==========================================================================
    // Per-key chord recipes. One packed, lock-free 32-bit atomic per MIDI note:
    // type (2 bits) | complexity (4) | voicing (2) | pitch+12 (5) | override
    // flag (bit 31). A zero value means "no override, use the global matrix".
    std::atomic<juce::uint32> keyRecipes[128] {};

    static constexpr juce::uint32 keyRecipeOverrideBit = 1u << 31;

   #if defined(__cpp_lib_atomic_is_always_lock_free)
    static_assert (std::atomic<juce::uint32>::is_always_lock_free,
                   "Per-key recipes require lock-free 32-bit atomics");
   #endif

    static juce::uint32 packKeyRecipe (const ChordRecipe& recipe) noexcept;
    static ChordRecipe unpackKeyRecipe (juce::uint32 packed) noexcept;

    juce::ValueTree createKeyRecipeState() const;
    void restoreKeyRecipeState (const juce::ValueTree& state);

    //==========================================================================
    // Chord engine state. Everything below is touched only by the audio thread.
    static constexpr int maxChordTones = 8;
    static constexpr int maxActiveChords = 128;
    static constexpr int midiChannels = 16;

    struct ActiveChord
    {
        int triggerNote = -1; // -1 marks a free slot.
        int channel = 0;
        int numNotes = 0;
        juce::uint8 notes[maxChordTones] {};
    };

    ActiveChord activeChords[maxActiveChords];
    juce::uint8 noteRefCounts[midiChannels][128] {};
    bool wasBypassed = false;

    void resetChordState() noexcept;
    int buildChordNotes (int triggerNote, int chordType, int complexity,
                         int voicing, int pitchOffset,
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
