// =============================================================================
//  plugin_stress.cpp — extreme-parameter stability test, for ANY JUCE plugin.
//  Quantum Research Audio.
//
//  Uses only the juce::AudioProcessor interface (createPluginFilter, the
//  parameter list, processBlock, state), so it runs unchanged on every QRA
//  plugin: link it against the plugin's PluginMain.cpp like flux_plugin_smoke.
//
//  What it does (all deterministic, seeded):
//   A. every parameter alone at its extremes (0, 1, and the middle), the rest
//      at default, against hostile signals;
//   B. random corner combinations (every parameter at 0 or 1, a few at 0.5);
//   C. a hand-picked worst case (everything that adds gain or drive maxed);
//   D. automation torture: every parameter jumps to a random value every block;
//   E. sample rates 44.1 / 48 / 96 / 192 kHz and block sizes 1 .. 4096
//      (including blocks larger than announced);
//   F. state save / reload mid-stream; mono.
//  Hostile signals: silence, -140 dBFS (denormal bait), 0 dBFS square, +12 dBFS
//  sine at 20 Hz, DC, full-scale white noise, impulses, Nyquist (+1/-1).
//
//  Pass = output always finite; never above +24 dBFS; after the input stops the
//  tail keeps shrinking (no oscillation, no latch-up): the peak in seconds 4-5
//  of silence is under 80 % of seconds 2-3 and 30 dB under the case's peak, or
//  below -100 dBFS, or within 12 dB of the same settings' own silence floor (so a
//  plugin with a noise/hiss option is judged against its own hiss). (Transformer/capacitor-coupled analog may legitimately
//  take seconds to settle after DC or sub-bass overload; it must not ring or
//  stick.) Every case also has to run faster than real time.
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include <chrono>
#include <cstdio>
#include <random>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {

int failures = 0, cases = 0;
double worstPeak = 0, worstRealtime = 0;
std::string worstPeakCase, worstRtCase;

enum Sig { Silence, Tiny, Square0, Sine12, Dc, Noise, Impulses, Nyquist, kNumSigs };
const char* sigName[] = { "silence", "-140 dBFS", "0 dBFS square", "+12 dBFS 20 Hz", "DC 0.5", "0 dBFS noise", "impulses", "Nyquist" };

float sample (Sig s, long n, double fs, std::mt19937& rng)
{
    const double t = n / fs;
    switch (s)
    {
        case Silence:  return 0.0f;
        case Tiny:     return (float) (1.0e-7 * std::sin (2 * juce::MathConstants<double>::pi * 440 * t));
        case Square0:  return std::fmod (t * 50.0, 1.0) < 0.5 ? 1.0f : -1.0f;
        case Sine12:   return (float) (3.98 * std::sin (2 * juce::MathConstants<double>::pi * 20 * t));
        case Dc:       return 0.5f;
        case Noise:    return std::uniform_real_distribution<float> (-1.0f, 1.0f) (rng);
        case Impulses: return (n % (long) (fs / 10)) == 0 ? 1.0f : 0.0f;
        case Nyquist:  return (n & 1) ? 1.0f : -1.0f;
        default:       return 0.0f;
    }
}

std::unique_ptr<juce::AudioProcessor> make (double fs, int block, int channels)
{
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    p->setPlayConfigDetails (channels, channels, fs, block);
    p->prepareToPlay (fs, block);
    return p;
}

void setNorm (juce::AudioProcessor& p, int index, float v)
{
    auto* prm = p.getParameters()[index];
    prm->beginChangeGesture();
    prm->setValueNotifyingHost (v);
    prm->endChangeGesture();
}

// Run one case: `seconds` of signal, then up to 5 s of silence that must decay.
// `automate`: every block, every parameter jumps to a random value.
bool runCase (juce::AudioProcessor& p, Sig sig, double fs, int block, int channels,
              double seconds, const std::string& label, bool automate = false, unsigned seed = 1)
{
    ++cases;
    std::mt19937 rng (seed);
    juce::AudioBuffer<float> buf (channels, block);
    juce::MidiBuffer midi;
    double peak = 0, procTime = 0;
    bool finite = true;
    const long sigSamples = (long) (seconds * fs), tailSamples = (long) (5.0 * fs);
    long n = 0;
    double tailA = 0.0, tailB = 0.0;    // peaks in seconds 2-3 and 4-5 of silence
    long quietSince = -1;
    for (; n < sigSamples + tailSamples; n += block)
    {
        if (automate && n < sigSamples)
            for (int k = 0; k < p.getParameters().size(); ++k)
                p.getParameters()[k]->setValueNotifyingHost (std::uniform_real_distribution<float> (0, 1) (rng));
        for (int i = 0; i < block; ++i)
        {
            const float v = (n + i) < sigSamples ? sample (sig, n + i, fs, rng) : 0.0f;
            for (int c = 0; c < channels; ++c) buf.setSample (c, i, c == 1 ? 0.9f * v : v);
        }
        const auto t0 = std::chrono::high_resolution_clock::now();
        p.processBlock (buf, midi);
        procTime += std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        double blockPeak = 0;
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < block; ++i)
            {
                const float y = buf.getSample (c, i);
                if (! std::isfinite (y)) finite = false;
                blockPeak = std::max (blockPeak, (double) std::abs (y));
            }
        if (! finite) break;
        peak = std::max (peak, blockPeak);
        if (n >= sigSamples)
        {
            const double ts = (double) (n - sigSamples) / fs;
            if (ts >= 2.0 && ts < 3.0) tailA = std::max (tailA, blockPeak);
            if (ts >= 4.0) tailB = std::max (tailB, blockPeak);
            if (blockPeak < 1.0e-5) { if (quietSince < 0) quietSince = n; if (n - quietSince > (long) (0.2 * fs)) break; }
            else quietSince = -1;
        }
    }
    const double rt = procTime / ((double) n / fs);
    const bool bounded = peak <= 15.86;                     // +24 dBFS
    bool decays = tailB < 1.0e-5 || (tailB < 0.8 * tailA && tailB < peak * 0.0316);
    if (! decays && finite)
    {   // same settings, silence only: the plugin's own floor (hiss, etc.)
        std::unique_ptr<juce::AudioProcessor> ref (createPluginFilter());
        ref->setPlayConfigDetails (channels, channels, fs, block);
        ref->prepareToPlay (fs, block);
        for (int k = 0; k < p.getParameters().size(); ++k)
            ref->getParameters()[k]->setValueNotifyingHost (p.getParameters()[k]->getValue());
        juce::AudioBuffer<float> rb (channels, block);
        double floorPk = 0;
        for (long m = 0; m < (long) (5.0 * fs); m += block)
        {
            rb.clear(); ref->processBlock (rb, midi);
            if (m >= (long) (4.0 * fs)) floorPk = std::max (floorPk, (double) rb.getMagnitude (0, block));
        }
        decays = tailB <= 4.0 * floorPk + 1.0e-7;   // floor may sit a few dB higher after loud material (e.g. auto-gain correction, limited to 6 dB)
    }
    const double tailLevel = tailB;
    if (peak > worstPeak)     { worstPeak = peak; worstPeakCase = label + ", " + sigName[sig]; }
    if (rt > worstRealtime)   { worstRealtime = rt; worstRtCase = label + ", " + sigName[sig]; }
    if (! (finite && bounded && decays))
    {
        ++failures;
        std::printf ("  FAIL  %s, %s: %s%s%s (peak %.2f, tail %.2e)\n", label.c_str(), sigName[sig],
                     finite ? "" : "non-finite output ", bounded ? "" : "over +24 dBFS ",
                     decays ? "" : "does not decay ", peak, tailLevel);
        return false;
    }
    return true;
}

} // namespace

// Debug: flux_plugin_stress trace <signal 0-7> <v1> <v2> ... (normalised values
// for every parameter, in order) -> prints the output envelope for 10 s.
static int trace (int argc, char** argv)
{
    auto p = make (48000, 512, 2);
    const Sig sig = (Sig) std::atoi (argv[2]);
    for (int k = 0; k < p->getParameters().size() && k + 3 < argc; ++k) setNorm (*p, k, (float) std::atof (argv[k + 3]));
    std::mt19937 rng (1);
    juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
    double pk = 0; long n = 0;
    for (; n < 48000L * 10; n += 512)
    {
        for (int i = 0; i < 512; ++i) { const float v = n + i < 12000 ? sample (sig, n + i, 48000, rng) : 0.0f; b.setSample (0, i, v); b.setSample (1, i, 0.9f * v); }
        p->processBlock (b, m);
        pk = std::max (pk, (double) b.getMagnitude (0, 512));
        if ((n / 512) % 23 == 22) { std::printf ("t=%5.2f s  peak %.2e (%.0f dBFS)\n", n / 48000.0, pk, 20 * std::log10 (pk + 1e-30)); pk = 0; }
    }
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 2 && std::string (argv[1]) == "trace") return trace (argc, argv);
    const auto t0 = std::chrono::steady_clock::now();
    auto probe = make (48000, 512, 2);
    const int numParams = probe->getParameters().size();
    std::printf ("Plugin: %s, %d parameters, latency %d\n", probe->getName().toRawUTF8(), numParams, probe->getLatencySamples());
    std::vector<float> defaults;
    for (auto* prm : probe->getParameters())
    {
        defaults.push_back (prm->getDefaultValue());
        std::printf ("   %-12s steps %d\n", prm->getName (32).toRawUTF8(), prm->getNumSteps());
    }

    std::printf ("A. each parameter alone at 0 / 0.5 / 1, against every hostile signal\n");
    {
        int f0 = failures;
        for (int k = 0; k < numParams; ++k)
            for (float v : { 0.0f, 0.5f, 1.0f })
                for (int s = 0; s < kNumSigs; ++s)
                {
                    auto p = make (48000, 512, 2);
                    setNorm (*p, k, v);
                    runCase (*p, (Sig) s, 48000, 512, 2, 0.25,
                             juce::String (p->getParameters()[k]->getName (32) + " = " + juce::String (v, 1)).toStdString());
                }
        std::printf ("  %s  every parameter alone at its extremes (%d cases)\n", failures == f0 ? "PASS" : "FAIL", numParams * 3 * kNumSigs);
    }

    std::printf ("B. 120 random corner combinations (each parameter at 0 or 1, some at 0.5)\n");
    {
        int f0 = failures; std::mt19937 rng (512);
        for (int c = 0; c < 120; ++c)
        {
            auto p = make (48000, 512, 2);
            juce::String desc;
            for (int k = 0; k < numParams; ++k)
            {
                const int r = (int) (rng() % 5);
                const float v = r < 2 ? 0.0f : r < 4 ? 1.0f : 0.5f;
                setNorm (*p, k, v);
                desc << juce::String (v, 1) << " ";
            }
            const Sig s = (Sig) (c % kNumSigs == Silence ? Sine12 : c % kNumSigs);
            runCase (*p, s, 48000, 512, 2, 0.25, ("combo [" + desc.trim() + "]").toStdString());
        }
        std::printf ("  %s  random corner combinations\n", failures == f0 ? "PASS" : "FAIL");
    }

    std::printf ("C. worst case: every parameter at 1 (all gain/drive maxed), then all at 0\n");
    {
        int f0 = failures;
        for (float v : { 1.0f, 0.0f })
            for (int s = 0; s < kNumSigs; ++s)
            {
                auto p = make (48000, 512, 2);
                for (int k = 0; k < numParams; ++k) setNorm (*p, k, v);
                runCase (*p, (Sig) s, 48000, 512, 2, 0.5, v > 0.5f ? "all parameters = 1" : "all parameters = 0");
            }
        std::printf ("  %s  all-max and all-min\n", failures == f0 ? "PASS" : "FAIL");
    }

    std::printf ("D. automation torture: every parameter jumps every block\n");
    {
        int f0 = failures;
        for (int block : { 32, 512 })
            for (Sig s : { Sine12, Noise, Square0 })
            {
                auto p = make (48000, block, 2);
                runCase (*p, s, 48000, block, 2, 1.0, "random automation, block " + std::to_string (block), true, (unsigned) block);
            }
        std::printf ("  %s  random jumps on every parameter every block\n", failures == f0 ? "PASS" : "FAIL");
    }

    std::printf ("E. sample rates and block sizes (gain/drive maxed, +12 dBFS 20 Hz and noise)\n");
    {
        int f0 = failures;
        for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (int block : { 1, 7, 64, 512, 4096 })
            {
                if (block == 1 && fs > 48000.0) continue;              // keep CI time sane
                auto p = make (fs, block == 4096 ? 1024 : block, 2);    // 4096: larger than announced
                for (int k = 0; k < numParams; ++k) setNorm (*p, k, 1.0f);
                for (Sig s : { Sine12, Noise })
                    runCase (*p, s, fs, block, 2, 0.2, std::to_string ((int) fs) + " Hz, block " + std::to_string (block));
            }
        std::printf ("  %s  44.1 to 192 kHz, blocks 1 to 4096 (incl. larger than prepared)\n", failures == f0 ? "PASS" : "FAIL");
    }

    std::printf ("F. state reload mid-stream, mono\n");
    {
        int f0 = failures;
        auto p = make (48000, 512, 2);
        std::mt19937 rng (7);
        for (int k = 0; k < numParams; ++k) setNorm (*p, k, std::uniform_real_distribution<float> (0, 1) (rng));
        juce::MemoryBlock state; p->getStateInformation (state);
        runCase (*p, Sine12, 48000, 512, 2, 0.2, "before reload");
        p->setStateInformation (state.getData(), (int) state.getSize());
        runCase (*p, Noise, 48000, 512, 2, 0.2, "after reload");
        auto m = make (44100, 256, 1);
        for (int k = 0; k < numParams; ++k) setNorm (*m, k, 1.0f);
        for (int s = 0; s < kNumSigs; ++s) runCase (*m, (Sig) s, 44100, 256, 1, 0.2, "mono, all maxed");
        std::printf ("  %s  state reload and mono\n", failures == f0 ? "PASS" : "FAIL");
    }

    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("\n%d cases in %.0f s. Loudest output %.2f (%+.1f dBFS) in [%s]. Heaviest case %.0f%% of real time [%s]\n",
                 cases, secs, worstPeak, 20 * std::log10 (worstPeak + 1e-12), worstPeakCase.c_str(), 100 * worstRealtime, worstRtCase.c_str());
    if (worstRealtime >= 1.0) { ++failures; std::printf ("  FAIL  a case ran slower than real time\n"); }
    std::printf ("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
