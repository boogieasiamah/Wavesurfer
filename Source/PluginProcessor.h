#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/WaveSurferDSP.h"
#include <mutex>
#include <thread>

// ==============================================================================
// WAVESURFER — audio processor
// Parameter IDs from the original SpectralUnmasker are kept (recover, tame, bias,
// brighten, boost, amount, lowLimit, highLimit).
// v1.1: Recover/Tame go to 200%; Boost is a listening-level shift (ISO 226, +-50 dB);
// new: output, reference (Ear/Pink), lookahead, gainMatch (loudness, default on),
// delta, bypass, clip.
// ==============================================================================
class WaveSurferProcessor : public juce::AudioProcessor, private juce::AsyncUpdater {
public:
    WaveSurferProcessor();
    ~WaveSurferProcessor() override;

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
    void handleAsyncUpdate() override { setLatencySamples(pendingLatency.load()); }

    const wavesurfer::WaveSurferEngine& getEngine() const noexcept { return engine; }
    // Fresh responseDb from the engine's published bell gains (message-thread safe).
    float curveDbAt(double freq) const noexcept;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ---- Ref Track: load a song, analyse its long-term balance off the audio thread ----------
    enum class RefStatus { None = 0, Analysing, Ready, Failed };
    void loadReferenceFile(const juce::File& file);      // message thread; analysis runs on a worker
    void clearReference();                                // message thread
    RefStatus getRefStatus() const noexcept { return (RefStatus) refStatus.load(); }
    juce::String getRefName() const;                      // message thread
    juce::String getRefError() const;                     // message thread

private:
    wavesurfer::WaveSurferEngine engine;

    std::atomic<float>* pRecover = nullptr; std::atomic<float>* pTame = nullptr;
    std::atomic<float>* pBias = nullptr;    std::atomic<float>* pBrighten = nullptr;
    std::atomic<float>* pBoost = nullptr;   std::atomic<float>* pAmount = nullptr;
    std::atomic<float>* pLow = nullptr;     std::atomic<float>* pHigh = nullptr;
    std::atomic<float>* pGainMatch = nullptr; std::atomic<float>* pDelta = nullptr;
    std::atomic<float>* pBypass = nullptr;  std::atomic<float>* pClip = nullptr;
    std::atomic<float>* pOutput = nullptr;  std::atomic<float>* pReference = nullptr;
    std::atomic<float>* pLookahead = nullptr;
    std::atomic<int> pendingLatency { 0 };
    int reportedLatency = 0;
    juce::AudioProcessorParameter* bypassParam = nullptr;

    // Ref Track state. The curve lives in apvts.state (saved with the session); refF/refD mirror it
    // so prepareToPlay can re-map it onto the engine's bands at a new sample rate.
    void applyReferenceToEngine();
    void storeReference(const juce::String& name, const std::vector<float>& f, const std::vector<float>& d);
    void restoreReferenceFromState();
    std::mutex refMutex;                       // guards refF/refD/refError (never touched by the audio thread)
    std::vector<float> refF, refD;
    juce::String refError;
    std::atomic<int> refStatus { 0 };
    std::atomic<bool> cancelAnalysis { false };
    std::thread analysisThread;

    JUCE_DECLARE_WEAK_REFERENCEABLE(WaveSurferProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveSurferProcessor)
};
