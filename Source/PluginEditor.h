#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ChordSettingsPanel.h"
#include "UI/BoilerplateLookAndFeel.h"

class PluginProcessor;

class PluginEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor& processor);
    ~PluginEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    int scaled (int value) const noexcept;

    PluginProcessor& processorRef;
    boilerplate::ui::BoilerplateLookAndFeel lookAndFeel;

    // Declaration order matters: the panel owns parameter attachments that
    // must be destroyed before the controls they point at.
    ChordSettingsPanel chordPanel;

    juce::Slider gainSlider;
    juce::ToggleButton bypassButton;

    // Declared after their controls so the attachments are destroyed first.
    juce::AudioProcessorValueTreeState::SliderAttachment gainAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
