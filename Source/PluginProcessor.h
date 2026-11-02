#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

class PluginProcessor final : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

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

private:
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
