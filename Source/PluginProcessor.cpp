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
    pOutput   = apvts.getRawParameterValue("output");
    pReference= apvts.getRawParameterValue("reference");
    pLookahead= apvts.getRawParameterValue("lookahead");
    bypassParam = apvts.getParameter("bypass");
    jassert(pRecover && pTame && pBias && pBrighten && pBoost && pAmount && pLow && pHigh
            && pGainMatch && pDelta && pBypass && pClip && pOutput && pReference && pLookahead && bypassParam);
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
    auto dbInt = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { const int n = juce::roundToInt(v); return (n > 0 ? "+" : "") + String(n) + " dB"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return v >= 1000.0f ? String(v / 1000.0f, 1) + " kHz" : String(juce::roundToInt(v)) + " Hz"; });

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"recover", 1}, "Recover",
        NormalisableRange<float>(0.0f, 2.0f), 0.0f, pct));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"tame", 1}, "Tame",
        NormalisableRange<float>(0.0f, 2.0f), 0.0f, pct));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"bias", 1}, "Bias",
        NormalisableRange<float>(-1.0f, 1.0f), 0.0f, bip));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"brighten", 1}, "Brighten",
        NormalisableRange<float>(-1.0f, 1.0f), 0.0f, bip));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"boost", 1}, "Boost",
        NormalisableRange<float>(-50.0f, 50.0f), 0.0f, dbInt));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"output", 1}, "Output",
        NormalisableRange<float>(-12.0f, 12.0f), 0.0f, db));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"reference", 1}, "Reference",
        StringArray { "Ear (ISO 226)", "Pink Noise", "Ref Track" }, 0));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"lookahead", 1}, "Lookahead", false));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"amount", 1}, "Amount",
        NormalisableRange<float>(0.0f, 1.0f), 1.0f, pct));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"lowLimit", 1}, "Low Limit",
        NormalisableRange<float>(20.0f, 2000.0f, 0.0f, 0.3f), 20.0f, hz));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"highLimit", 1}, "High Limit",
        NormalisableRange<float>(1000.0f, 20000.0f, 0.0f, 0.3f), 20000.0f, hz));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"gainMatch", 1}, "Loudness Match", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"delta", 1}, "Delta", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"bypass", 1}, "Bypass", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{"clip", 1}, "Safety Clip", false));
    return layout;
}

void WaveSurferProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate, samplesPerBlock);   // sets fs, snaps smoothers on first block
    applyReferenceToEngine();                       // bands depend on the rate: re-map the stored curve
    reportedLatency = pLookahead->load() > 0.5f ? engine.getLookaheadSamples() : 0;
    pendingLatency = reportedLatency;
    setLatencySamples(reportedLatency);
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
    p.outputDb   = pOutput->load(std::memory_order_relaxed);
    {   const int r = juce::roundToInt(pReference->load(std::memory_order_relaxed));
        p.reference = r == 2 ? wavesurfer::Reference::Track : r == 1 ? wavesurfer::Reference::Pink : wavesurfer::Reference::Ear; }
    p.lookahead  = pLookahead->load(std::memory_order_relaxed) > 0.5f;
    engine.setParameters(p);

    // lookahead changes the latency: tell the host from the message thread
    const int lat = engine.getLatencySamples();
    if (lat != reportedLatency) { reportedLatency = lat; pendingLatency = lat; triggerAsyncUpdate(); }

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
        if (xml->hasTagName(apvts.state.getType())) {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            restoreReferenceFromState();
        }
}

// ---------------------------------------------------------------- Ref Track ----
WaveSurferProcessor::~WaveSurferProcessor()
{
    cancelAnalysis = true;
    if (analysisThread.joinable()) analysisThread.join();
}

juce::String WaveSurferProcessor::getRefName() const { return apvts.state.getProperty("refName").toString(); }
juce::String WaveSurferProcessor::getRefError() const { return refError; }

void WaveSurferProcessor::applyReferenceToEngine()
{
    std::lock_guard<std::mutex> lock(refMutex);
    if (refF.size() >= 2 && refF.size() == refD.size()) engine.setReferenceCurve(refF.data(), refD.data(), (int) refF.size());
    else engine.setReferenceCurve(nullptr, nullptr, 0);
}

void WaveSurferProcessor::storeReference(const juce::String& name, const std::vector<float>& f, const std::vector<float>& d)
{
    {
        std::lock_guard<std::mutex> lock(refMutex);
        refF = f; refD = d;
    }
    juce::String curve;
    for (size_t i = 0; i < f.size(); ++i) curve << juce::String(f[i], 2) << ":" << juce::String(d[i], 3) << " ";
    apvts.state.setProperty("refName", name, nullptr);
    apvts.state.setProperty("refCurve", curve.trim(), nullptr);
    applyReferenceToEngine();
    refStatus = f.empty() ? (int) RefStatus::None : (int) RefStatus::Ready;
}

void WaveSurferProcessor::restoreReferenceFromState()
{
    std::vector<float> f, d;
    for (auto& tok : juce::StringArray::fromTokens(apvts.state.getProperty("refCurve").toString(), " ", "")) {
        const int c = tok.indexOfChar(':');
        if (c <= 0) continue;
        const float fv = tok.substring(0, c).getFloatValue(), dv = tok.substring(c + 1).getFloatValue();
        if (std::isfinite(fv) && std::isfinite(dv) && fv > 0.0f) { f.push_back(fv); d.push_back(dv); }
    }
    {
        std::lock_guard<std::mutex> lock(refMutex);
        refF = f; refD = d;
    }
    applyReferenceToEngine();
    refStatus = f.size() >= 2 ? (int) RefStatus::Ready : (int) RefStatus::None;
}

void WaveSurferProcessor::clearReference()
{
    cancelAnalysis = true;
    if (analysisThread.joinable()) analysisThread.join();
    storeReference({}, {}, {});
    apvts.state.removeProperty("refName", nullptr);
    apvts.state.removeProperty("refCurve", nullptr);
}

void WaveSurferProcessor::loadReferenceFile(const juce::File& file)
{
    cancelAnalysis = true;
    if (analysisThread.joinable()) analysisThread.join();
    cancelAnalysis = false;
    refStatus = (int) RefStatus::Analysing;

    juce::WeakReference<WaveSurferProcessor> weak(this);
    analysisThread = std::thread([this, weak, file] {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();                                   // WAV, AIFF, FLAC, Ogg, MP3; CoreAudio formats on macOS
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
        std::vector<float> f(wavesurfer::WaveSurferEngine::NUM_BANDS), d(f.size());
        juce::String err;
        bool ok = false;
        if (reader == nullptr) err = "Can't read this file type";
        else if (reader->sampleRate < 22050.0 || reader->lengthInSamples < (juce::int64) (reader->sampleRate * 5.0))
            err = "Reference must be at least 5 s long, 22 kHz or higher";
        else {
            auto an = std::make_unique<wavesurfer::WaveSurferEngine>();   // separate engine: same filter bank, same calibration
            an->prepare(reader->sampleRate, 4096);
            an->setParameters(wavesurfer::Params {});
            an->beginAnalysis();
            const juce::int64 maxLen = juce::jmin(reader->lengthInSamples, (juce::int64) (reader->sampleRate * 900.0));   // 15 min cap
            juce::AudioBuffer<float> buf(2, 4096);
            for (juce::int64 pos = 0; pos < maxLen && ! cancelAnalysis.load(); pos += 4096) {
                const int n = (int) juce::jmin<juce::int64>(4096, maxLen - pos);
                reader->read(&buf, 0, n, pos, true, reader->numChannels > 1);
                if (reader->numChannels == 1) buf.copyFrom(1, 0, buf, 0, 0, n);
                float* ch[2] = { buf.getWritePointer(0), buf.getWritePointer(1) };
                an->process(ch, 2, n);
            }
            ok = ! cancelAnalysis.load() && an->getAnalysis(f.data(), d.data());
            if (! ok && ! cancelAnalysis.load()) err = "Reference is silent";
        }
        if (cancelAnalysis.load()) return;
        const auto name = file.getFileNameWithoutExtension();
        juce::MessageManager::callAsync([weak, ok, f, d, err, name] {
            auto* self = weak.get();
            if (self == nullptr) return;
            if (ok) self->storeReference(name, f, d);
            else { { std::lock_guard<std::mutex> lock(self->refMutex); self->refError = err; } self->refStatus = (int) RefStatus::Failed; }
        });
    });
}

juce::AudioProcessorEditor* WaveSurferProcessor::createEditor()
{
    return new WaveSurferEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WaveSurferProcessor();
}
