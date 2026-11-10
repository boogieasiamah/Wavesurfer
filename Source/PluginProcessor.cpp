#include "PluginProcessor.h"
#include "PluginEditor.h"

WaveSurferProcessor::WaveSurferProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    // FIX: cache raw parameter pointers once instead of string lookups per block
    pRecover  = apvts.getRawParameterValue("recover");
    pTame     = apvts.getRawParameterValue("tame");
    pBias     = apvts.getRawParameterValue("bias");
    pBrighten = apvts.getRawParameterValue("brighten");
    pBoost    = apvts.getRawParameterValue("boost");
    pAmount   = apvts.getRawParameterValue("amount");
    pLow      = apvts.getRawParameterValue("lowLimit");
    pHigh     = apvts.getRawParameterValue("highLimit");
    pGainMatch= apvts.getRawParameterValue("gainMatch");
    pDelta    = apvts.getRawParameterValue("delta");
    pBypass   = apvts.getRawParameterValue("bypass");
    pClip     = apvts.getRawParameterValue("clip");
    bypassParam = apvts.getParameter("bypass");
    jassert(pRecover && pTame && pBias && pBrighten && pBoost && pAmount && pLow && pHigh
            && pGainMatch && pDelta && pBypass && pClip && bypassParam);
}

juce::AudioProcessorValueTreeState::ParameterLayout WaveSurferProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return String(juce::roundToInt(v * 100.0f)) + "%"; });
    auto bip = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { auto n = juce::roundToInt(v * 100.0f); return (n > 0 ? "+" : "") + String(n) + "%"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return (v > 0.0f ? "+" : "") + String(v, 1) + " dB"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return v >= 1000.0f ? String(v / 1000.0f, 1) + " kHz" : String(juce::roundToInt(v)) + " Hz"; });

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"recover", 1}, "Recover",
        NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"tame", 1}, "Tame",
        NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"bias", 1}, "Bias",
        NormalisableRange<float>(-1.0f, 1.0f), 0.0f, bip));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"brighten", 1}, "Brighten",
        NormalisableRange<float>(-1.0f, 1.0f), 0.0f, bip));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"boost", 1}, "Boost",
        NormalisableRange<float>(-12.0f, 12.0f), 0.0f, db));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"amount", 1}, "Amount",
        NormalisableRange<float>(0.0f, 1.0f), 1.0f, pct));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"lowLimit", 1}, "Low Limit",
        NormalisableRange<float>(20.0f, 2000.0f, 0.0f, 0.3f), 20.0f, hz));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"highLimit", 1}, "High Limit",
        NormalisableRange<float>(1000.0f, 20000.0f, 0.0f, 0.3f), 20000.0f, hz));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"gainMatch", 1}, "Gain Match", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"delta", 1}, "Delta", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"bypass", 1}, "Bypass", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"clip", 1}, "Safety Clip", false));
    return layout;
}

void WaveSurferProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate, samplesPerBlock);   // sets fs, snaps smoothers on first block
    setLatencySamples(0);
}

bool WaveSurferProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void WaveSurferProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    wavesurfer::Params p;
    p.recover    = pRecover->load(std::memory_order_relaxed);
    p.tame       = pTame->load(std::memory_order_relaxed);
    p.bias       = pBias->load(std::memory_order_relaxed);
    p.brighten   = pBrighten->load(std::memory_order_relaxed);
    p.boostDb    = pBoost->load(std::memory_order_relaxed);
    p.amount     = pAmount->load(std::memory_order_relaxed);
    p.lowHz      = pLow->load(std::memory_order_relaxed);
    p.highHz     = pHigh->load(std::memory_order_relaxed);
    p.gainMatch  = pGainMatch->load(std::memory_order_relaxed) > 0.5f;
    p.delta      = pDelta->load(std::memory_order_relaxed) > 0.5f;
    p.bypass     = pBypass->load(std::memory_order_relaxed) > 0.5f;
    p.safetyClip = pClip->load(std::memory_order_relaxed) > 0.5f;
    engine.setParameters(p);

    const int nch = juce::jmin(numIn, numOut, buffer.getNumChannels(), 2);
    engine.process(buffer.getArrayOfWritePointers(), nch, buffer.getNumSamples());
}

float WaveSurferProcessor::curveDbAt(double f) const noexcept
{
    const auto& s = engine.getSnapshot();
    const double fs = s.sampleRate.load(std::memory_order_relaxed);
    const double q  = s.bellQ.load(std::memory_order_relaxed);
    float sum = 0.0f;
    for (int i = 0; i < wavesurfer::WaveSurferEngine::NUM_BELLS; ++i)
        sum += wavesurfer::bellMagnitudeDb(f, s.bellFreq[(size_t) i].load(std::memory_order_relaxed), q,
                                           s.bellGainDb[(size_t) i].load(std::memory_order_relaxed), fs);
    return sum;
}

void WaveSurferProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void WaveSurferProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* WaveSurferProcessor::createEditor()
{
    return new WaveSurferEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WaveSurferProcessor();
}
