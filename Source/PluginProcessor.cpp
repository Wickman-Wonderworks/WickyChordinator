#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "State/ParameterIDs.h"
#include "State/ParameterLayout.h"

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", boilerplate::parameters::createLayout())
{
    gainDbParameter = apvts.getRawParameterValue (boilerplate::parameters::gain);
    bypassParameter = apvts.getRawParameterValue (boilerplate::parameters::bypass);

    jassert (gainDbParameter != nullptr);
    jassert (bypassParameter != nullptr);
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
    return apvts.getParameter (boilerplate::parameters::bypass);
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
    juce::ignoreUnused (samplesPerBlock);

    const auto initialGain = gainDbParameter != nullptr ? gainDbParameter->load() : 0.0f;
    gainProcessor.prepare (sampleRate, initialGain);
}

void PluginProcessor::releaseResources()
{
    gainProcessor.reset();
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto outputChannels = layouts.getMainOutputChannelSet();

    if (outputChannels != juce::AudioChannelSet::mono()
        && outputChannels != juce::AudioChannelSet::stereo())
        return false;

    return outputChannels == layouts.getMainInputChannelSet();
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Outputs without a matching input may contain garbage; clear them.
    for (auto channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    const auto gainDecibels = gainDbParameter != nullptr ? gainDbParameter->load() : 0.0f;
    const auto bypassed = bypassParameter != nullptr && bypassParameter->load() > 0.5f;

    gainProcessor.process (buffer, gainDecibels, bypassed);
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
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();

    if (!state.isValid())
        return;

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto state = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes));

    if (state.isValid() && state.hasType (apvts.state.getType()))
        apvts.replaceState (state);
}

//==============================================================================
// This creates the plugin instance. The host calls it, so it must have C linkage
// and live outside any namespace.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
