#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ChordSettingsPanel.h"
#include "UI/BoilerplateLookAndFeel.h"

class PluginProcessor;

class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           private juce::MidiKeyboardState::Listener
{
public:
    explicit PluginEditor (PluginProcessor& processor);
    ~PluginEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    void timerCallback() override;

    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel,
                       int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel,
                        int midiNoteNumber, float velocity) override;

    //==========================================================================
    int scaled (int value) const noexcept;

    void configureStatusBar();
    void layoutStatusBar (juce::Rectangle<int> area);
    void refreshChordReadout();
    void selectPreset (int index);
    juce::String currentChordSymbol() const;
    static juce::String chordSymbolFor (int rootNote,
                                        const PluginProcessor::ChordRecipe& recipe);

    //==========================================================================
    PluginProcessor& processorRef;
    boilerplate::ui::BoilerplateLookAndFeel lookAndFeel;
    std::unique_ptr<wicky::ui::PresetManager> presetManager;

    // Top status bar.
    juce::Label presetCaption;
    juce::ComboBox presetSelector;
    juce::TextButton presetPreviousButton;
    juce::TextButton presetNextButton;
    juce::Label chordCaption;
    juce::Label chordReadout;
    juce::ToggleButton powerButton;
    juce::Slider gainSlider;

    // Main two-column surface plus the shared key-map/keyboard strip.
    wicky::ui::ChordSettingsPanel chordPanel;
    wicky::ui::GlobalSettingsPanel globalPanel;
    wicky::ui::KeyMapBar keyMapBar;
    wicky::ui::ChordKeyboard chordKeyboard;

    // Declared after their controls so the attachments are destroyed first.
    juce::AudioProcessorValueTreeState::SliderAttachment gainAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment powerAttachment;

    juce::Rectangle<int> statusArea;
    juce::Rectangle<int> leftColumnArea;
    juce::Rectangle<int> rightColumnArea;
    juce::Rectangle<int> keyMapArea;
    juce::Rectangle<int> keyboardArea;
    juce::Rectangle<int> footerArea;

    bool lastSoundState = false;
    bool updatingPresetSelector = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
