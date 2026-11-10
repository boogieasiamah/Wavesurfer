#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/WaveSurferDSP.h"

// ==============================================================================
// WAVESURFER — audio processor
// Parameter IDs from the original SpectralUnmasker are kept (recover, tame, bias,
// brighten, boost, amount, lowLimit, highLimit). Brighten is now bipolar (-1..1,
// default 0, so old sessions load unchanged). New: gainMatch, delta, bypass, clip.
// ==============================================================================
class WaveSurferProcessor : public juce::AudioProcessor {
public:
    WaveSurferProcessor();
    ~WaveSurferProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "WaveSurfer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }   // bell ring-out
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    const wavesurfer::WaveSurferEngine& getEngine() const noexcept { return engine; }
    // Fresh responseDb from the engine's published bell gains (message-thread safe).
    float curveDbAt(double freq) const noexcept;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    wavesurfer::WaveSurferEngine engine;

    std::atomic<float>* pRecover = nullptr; std::atomic<float>* pTame = nullptr;
    std::atomic<float>* pBias = nullptr;    std::atomic<float>* pBrighten = nullptr;
    std::atomic<float>* pBoost = nullptr;   std::atomic<float>* pAmount = nullptr;
    std::atomic<float>* pLow = nullptr;     std::atomic<float>* pHigh = nullptr;
    std::atomic<float>* pGainMatch = nullptr; std::atomic<float>* pDelta = nullptr;
    std::atomic<float>* pBypass = nullptr;  std::atomic<float>* pClip = nullptr;
    juce::AudioProcessorParameter* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveSurferProcessor)
};
