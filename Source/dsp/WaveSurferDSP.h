#pragma once
// ==============================================================================
// WAVESURFER — perceptual spectral unmasking engine (JUCE-free, header-only)
//
// Signal flow (audio thread):
//   in ─┬─────────────────────────────────────────────── dry (bypass / delta)
//       ├─ Mid ─ DC blocker ─ 32 TPT band-pass (analysis only, never summed)
//       │        └─ mean-square per control block ─ Kalman (dB) ─ masking model
//       │                                                         │
//       └─ 16 TPT bell cascade (per channel) ◄── band gains ◄─────┘
//            └─ gain match ─ Boost ─ delta / bypass crossfade ─ safety clip ─ out
//
// Everything expensive runs at CONTROL RATE (~1.5 kHz, every N samples):
// Kalman, perceptual weighting, two-pass spreading, gain law, bell coefficients.
// Audio rate cost = 32 SVF band-passes (mono) + 16 bells x channels.
// ==============================================================================
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>

namespace wavesurfer {

// Deliberate exact comparisons (the bit-exact null path depends on them).
template <typename T> inline bool exactly(T a, T b) noexcept { return std::equal_to<T>()(a, b); }

// ==============================================================================
// TRANSPARENT SOFT CLIPPER (unchanged from the original — verified correct)
// Unity through |x| <= 0.9, soft knee to exactly 1.0 at |x| == 1.2, hard clip after.
// fastTanh's Padé fit is only tanh-like on [-3, 3]; at ±3 it hits ±1 with zero slope.
// d/dx = 9(x²-9)² / (27+9x²)²  -> monotonic, C1 at both ends of the knee.
// ==============================================================================
inline float fastTanh(float x) {
    x = std::clamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float transparentSoftClip(float x) {
    const float threshold = 0.9f;
    const float ax = std::abs(x);
    if (ax <= threshold)
        return x;

    const float sign = (x < 0.0f) ? -1.0f : 1.0f;
    const float headroom = 1.0f - threshold;
    const float shaped = std::min(1.0f, fastTanh((ax - threshold) / headroom));
    return sign * (threshold + headroom * shaped);
}

// ==============================================================================
// KALMAN FILTER FOR ENVELOPE TRACKING (kept, fixed)
// With constant Q and R a scalar random-walk Kalman converges to a one-pole with
// steady-state gain K. We choose K from a time constant, then solve Q exactly:
//     K = 1 - exp(-1 / (tau * rate)),   Q = K² R / (1 - K)
// so the time constant is identical at every sample rate (old code scaled Q by
// 1/fs, which is wrong: K ~ sqrt(Q/R), so tau drifted by sqrt(rate ratio)).
// Runs at control rate on dB levels.
// ==============================================================================
struct KalmanEnvelope {
    float x = -120.0f;
    float P = 1.0f;
    float Q = 0.001f;
    float R = 0.05f;

    void reset(float initial = -120.0f) noexcept { x = initial; P = 1.0f; }

    void prepare(double updateRateHz, double tauSeconds) noexcept {
        const double rate = std::max(1.0, updateRateHz);
        const double tau  = std::max(1.0e-4, tauSeconds);
        const double K    = std::clamp(1.0 - std::exp(-1.0 / (tau * rate)), 1.0e-7, 0.999);
        R = 0.05f;
        Q = static_cast<float>(K * K * R / (1.0 - K));
        reset();
    }

    float process(float measurement) noexcept {
        if (! std::isfinite(measurement))   // NaN guard: hold state
            return x;
        const float P_pred = P + Q;
        const float K = P_pred / (P_pred + R);
        x += K * (measurement - x);
        P = (1.0f - K) * P_pred;
        if (P < 1.0e-20f) P = 1.0e-20f;
        return x;
    }

    // Steady-state gain implied by the current Q/R (for tests).
    float steadyStateGain() const noexcept {
        const double q = Q, r = R;
        const double M = 0.5 * (q + std::sqrt(q * q + 4.0 * q * r));
        return static_cast<float>(M / (M + r));
    }
};

// ==============================================================================
// SMALL HELPERS
// ==============================================================================
struct LinearSmoother {           // replacement for juce::SmoothedValue (no JUCE dep)
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int   remaining = 0, rampLength = 1;

    void prepare(double rateHz, double seconds) noexcept {
        rampLength = std::max(1, static_cast<int>(std::lround(rateHz * seconds)));
        remaining = 0;
    }
    void setCurrentAndTarget(float v) noexcept { current = target = v; remaining = 0; step = 0.0f; }
    void setTarget(float v) noexcept {
        if (exactly(v, target)) return;
        target = v;
        remaining = rampLength;
        step = (target - current) / static_cast<float>(rampLength);
    }
    float next() noexcept {
        if (remaining > 0) { current += step; if (--remaining == 0) current = target; }
        return current;
    }
    // advance n steps at once (for control-rate use)
    float skip(int n) noexcept {
        if (remaining <= 0) return current;
        if (n >= remaining) { current = target; remaining = 0; }
        else { current += step * static_cast<float>(n); remaining -= n; }
        return current;
    }
    bool isSmoothing() const noexcept { return remaining > 0; }
};

inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }

// Zwicker critical-band rate (Bark)
inline double barkOf(double f) noexcept {
    return 13.0 * std::atan(0.00076 * f) + 3.5 * std::atan((f / 7500.0) * (f / 7500.0));
}

// Terhardt absolute threshold of hearing, dB SPL
inline double thresholdInQuietDb(double f) noexcept {
    const double k = std::clamp(f, 20.0, 22000.0) / 1000.0;
    return 3.64 * std::pow(k, -0.8) - 6.5 * std::exp(-0.6 * (k - 3.3) * (k - 3.3)) + 1.0e-3 * k * k * k * k;
}

// ISO 226:2003 equal-loudness contour: SPL needed at f for a given phon level.
inline double iso226SplDb(double f, double phon) noexcept {
    static constexpr double F[29]  = { 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400,
                                       500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000,
                                       6300, 8000, 10000, 12500 };
    static constexpr double AF[29] = { 0.532, 0.506, 0.480, 0.455, 0.432, 0.409, 0.387, 0.367, 0.349, 0.330,
                                       0.315, 0.301, 0.288, 0.276, 0.267, 0.259, 0.253, 0.250, 0.246, 0.244,
                                       0.243, 0.243, 0.243, 0.242, 0.242, 0.245, 0.254, 0.271, 0.301 };
    static constexpr double LU[29] = { -31.6, -27.2, -23.0, -19.1, -15.9, -13.0, -10.3, -8.1, -6.2, -4.5,
                                       -3.1, -2.0, -1.1, -0.4, 0.0, 0.3, 0.5, 0.0, -2.7, -4.1, -1.0, 1.7,
                                       2.5, 1.2, -2.1, -7.1, -11.2, -10.7, -3.1 };
    static constexpr double TF[29] = { 78.5, 68.7, 59.5, 51.1, 44.0, 37.5, 31.5, 26.5, 22.1, 17.9, 14.4,
                                       11.4, 8.6, 6.2, 4.4, 3.0, 2.2, 2.4, 3.5, 1.7, -1.3, -4.2, -6.0, -5.4,
                                       -1.5, 6.0, 12.6, 13.9, 12.3 };
    const double fc = std::clamp(f, 20.0, 12500.0);
    int i = 0;
    while (i < 27 && F[i + 1] < fc) ++i;
    const double t  = (std::log(fc) - std::log(F[i])) / (std::log(F[i + 1]) - std::log(F[i]));
    const double af = AF[i] + t * (AF[i + 1] - AF[i]);
    const double lu = LU[i] + t * (LU[i + 1] - LU[i]);
    const double tf = TF[i] + t * (TF[i + 1] - TF[i]);
    const double Af = 4.47e-3 * (std::pow(10.0, 0.025 * phon) - 1.15)
                    + std::pow(0.4 * std::pow(10.0, (tf + lu) / 10.0 - 9.0), af);
    double lp = (10.0 / af) * std::log10(Af) - lu + 94.0;
    if (f > 12500.0) // extend above the table with half the threshold rise, capped
        lp += std::min(15.0, 0.5 * (thresholdInQuietDb(f) - thresholdInQuietDb(12500.0)));
    return lp;
}

// Magnitude (dB) of a TPT/Simper bell at frequency f. Exact for the digital filter:
// TPT maps f -> analog w = tan(pi f/fs)/tan(pi fc/fs); H = (1-w²+j k A² w)/(1-w²+j k w)
inline float bellMagnitudeDb(double f, double fc, double q, double gainDb, double fs) noexcept {
    if (exactly(gainDb, 0.0)) return 0.0f;
    const double A  = std::pow(10.0, gainDb / 40.0);
    const double k  = 1.0 / (q * A);
    const double ny = 0.4999 * fs;
    const double w  = std::tan(3.14159265358979 * std::min(f, ny) / fs)
                    / std::tan(3.14159265358979 * std::min(fc, ny) / fs);
    const double re = 1.0 - w * w;
    const double num = re * re + (k * A * A * w) * (k * A * A * w);
    const double den = re * re + (k * w) * (k * w);
    return static_cast<float>(10.0 * std::log10(num / den));
}

// ==============================================================================
// PARAMETERS
// ==============================================================================
struct Params {
    float recover  = 0.0f;   // 0..1   lift masked bands
    float tame     = 0.0f;   // 0..1   cut dominant / resonant bands
    float bias     = 0.0f;   // -1..1  -1 favours Tame, +1 favours Recover
    float brighten = 0.0f;   // -1..1  perceptual tilt (darker <-> brighter)
    float boostDb  = 0.0f;   // -12..12 output gain
    float amount   = 1.0f;   // 0..1   scales the whole EQ curve
    float lowHz    = 20.0f;  // lower processing limit
    float highHz   = 20000.0f;
    bool  gainMatch  = false;
    bool  delta      = false;
    bool  bypass     = false;
    bool  safetyClip = false;
};

// ==============================================================================
// ENGINE
// ==============================================================================
class WaveSurferEngine {
public:
    static constexpr int NUM_BANDS = 32;
    static constexpr int NUM_BELLS = 16;
    static constexpr int MAX_CTRL  = 256;     // max control block (=scratch size)
    static constexpr float MAX_GAIN_DB = 12.0f;

    // GUI-readable state (written by audio thread, read by message thread)
    struct Snapshot {
        std::array<std::atomic<float>, NUM_BANDS> bandFreq {}, bandLevelDb {}, bandGainDb {};
        std::array<std::atomic<float>, NUM_BELLS> bellFreq {}, bellGainDb {};
        std::atomic<float> bellQ { 2.0f }, sampleRate { 48000.0f };
        std::atomic<float> inPeak[2] {}, outPeak[2] {};
        std::atomic<float> inRmsDb { -120.0f }, outRmsDb { -120.0f }, gainMatchDb { 0.0f };
        std::atomic<uint32_t> counter { 0 };
    };

    void prepare(double sampleRate, int /*maxBlock*/ = 512) {
        fs = std::max(8000.0, sampleRate);
        ctrlN = std::clamp(static_cast<int>(std::lround(fs / 1500.0)), 8, MAX_CTRL);
        ctrlRate = fs / ctrlN;

        // ---- band layout: 32 log-spaced bands, kept below 0.45 fs -------------
        fTop = std::min(20000.0, 0.45 * fs);
        fBot = 20.0;
        const double r = std::pow(fTop / fBot, 1.0 / (NUM_BANDS - 1));
        const double bandQ = std::sqrt(r) / (r - 1.0);
        for (int b = 0; b < NUM_BANDS; ++b) {
            const double fc = fBot * std::pow(r, b);
            bandFreq[b] = static_cast<float>(fc);
            const double g = std::tan(3.14159265358979 * fc / fs);
            const double k = 1.0 / bandQ;
            const double a1 = 1.0 / (1.0 + g * (g + k));
            ba1[b] = static_cast<float>(a1);
            ba2[b] = static_cast<float>(g * a1);
            ba3[b] = static_cast<float>(g * g * a1);
            bandNorm[b] = static_cast<float>(k * k);   // v1*k = unity-peak band-pass, squared

            // Kalman: slower for low bands so the envelope never follows the waveform
            const double tau = std::clamp(4.0 / fc, 0.015, 0.25);
            kalman[b].prepare(ctrlRate, tau);

            // perceptual tables
            weightDb[b] = static_cast<float>(iso226SplDb(1000.0, 70.0) - iso226SplDb(fc, 70.0));
            thresholdDb[b] = static_cast<float>(thresholdInQuietDb(fc));
            tiltShape[b] = static_cast<float>(std::clamp(std::log2(fc / 1000.0) / 4.0, -1.0, 1.0));
            log2Freq[b] = static_cast<float>(std::log2(fc));
        }

        // spreading coefficients (per neighbour step, in linear power)
        for (int b = 0; b < NUM_BANDS; ++b) {
            const double dzUp = b > 0 ? barkOf(bandFreq[b]) - barkOf(bandFreq[b - 1]) : 0.0;
            aUp[b] = static_cast<float>(std::pow(10.0, -SLOPE_UP_DB_PER_BARK * dzUp / 10.0));       // from b-1 -> b
            const double dzDn = b < NUM_BANDS - 1 ? barkOf(bandFreq[b + 1]) - barkOf(bandFreq[b]) : 0.0;
            aDn[b] = static_cast<float>(std::pow(10.0, -SLOPE_DOWN_DB_PER_BARK * dzDn / 10.0));     // from b+1 -> b
        }
        // masking ratio of a perceptually flat spectrum (normalisation reference)
        {
            float E[NUM_BANDS]; std::fill(E, E + NUM_BANDS, 1.0f);
            float Mo[NUM_BANDS]; othersMasking(E, Mo);
            for (int b = 0; b < NUM_BANDS; ++b)
                mr0[b] = -10.0f * std::log10(Mo[b] + 1.0e-30f);
        }

        // ---- bells: 16 log-spaced over the same range ------------------------
        const double rb = std::pow(fTop / fBot, 1.0 / (NUM_BELLS - 1));
        bellQ = std::sqrt(rb) / (rb - 1.0);
        for (int i = 0; i < NUM_BELLS; ++i) {
            bellFreq[i] = static_cast<float>(fBot * std::pow(rb, i));
            bellG[i] = static_cast<float>(std::tan(3.14159265358979 * bellFreq[i] / fs));
        }
        // overlap compensation: all bells at +6 dB should read ~+6 dB mid-range
        {
            double sum = 0.0; int n = 0;
            for (int i = 4; i < NUM_BELLS - 4; ++i) {
                double tot = 0.0;
                for (int j = 0; j < NUM_BELLS; ++j)
                    tot += bellMagnitudeDb(bellFreq[i], bellFreq[j], bellQ, 6.0, fs);
                sum += tot; ++n;
            }
            bellComp = static_cast<float>(6.0 / (sum / n));
        }

        // ---- DC blocker (analysis path only, so the audio path can null) -----
        dcR = static_cast<float>(std::exp(-2.0 * 3.14159265358979 * 5.0 / fs));

        // ---- smoothers -------------------------------------------------------
        const double ramp = 0.05;
        sRecover.prepare(ctrlRate, ramp);  sTame.prepare(ctrlRate, ramp);
        sBias.prepare(ctrlRate, ramp);     sBrighten.prepare(ctrlRate, ramp);
        sAmount.prepare(ctrlRate, ramp);   sLow.prepare(ctrlRate, 0.1);
        sHigh.prepare(ctrlRate, 0.1);
        sBoost.prepare(fs, ramp);
        sBypass.prepare(fs, 0.02);
        sDelta.prepare(fs, 0.02);
        gainSmoothCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (0.04 * ctrlRate)));
        gmPowCoeff  = static_cast<float>(1.0 - std::exp(-1.0 / (0.4 * ctrlRate)));
        gmGainCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (0.3 * ctrlRate)));

        // ---- publish static info for the GUI ---------------------------------
        for (int b = 0; b < NUM_BANDS; ++b) snap.bandFreq[(size_t) b].store(bandFreq[b], std::memory_order_relaxed);
        for (int i = 0; i < NUM_BELLS; ++i) snap.bellFreq[(size_t) i].store(bellFreq[i], std::memory_order_relaxed);
        snap.bellQ.store(static_cast<float>(bellQ), std::memory_order_relaxed);
        snap.sampleRate.store(static_cast<float>(fs), std::memory_order_relaxed);

        reset();
        paramsInitialised = false;   // first setParameters() snaps all smoothers
    }

    void reset() noexcept {
        std::fill(std::begin(bic1), std::end(bic1), 0.0f);
        std::fill(std::begin(bic2), std::end(bic2), 0.0f);
        std::fill(std::begin(acc), std::end(acc), 0.0f);
        for (auto& k : kalman) k.reset();
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < NUM_BELLS; ++i) { eic1[c][i] = 0.0f; eic2[c][i] = 0.0f; }
        for (int i = 0; i < NUM_BELLS; ++i) { ea1[i] = 1.0f / (1.0f + bellG[i] * (bellG[i] + 1.0f)); ea2[i] = bellG[i] * ea1[i]; ea3[i] = bellG[i] * ea2[i]; em1[i] = 0.0f; appliedBellDb[i] = 0.0f; bellActive[i] = false; }
        std::fill(std::begin(gainSmoothed), std::end(gainSmoothed), 0.0f);
        std::fill(std::begin(levelDb), std::end(levelDb), -120.0f);
        dcX1 = dcY1 = 0.0f;
        ctrlCount = 0;
        inMsAcc = outMsAcc = 0.0f;
        gmInPow = gmOutPow = 1.0e-12f;
        gmDb = 0.0f; gmGainCur = gmGainNext = 1.0f; gmStep = 0.0f;
        peakIn[0] = peakIn[1] = peakOut[0] = peakOut[1] = 0.0f;
    }

    void setParameters(const Params& p) noexcept {
        auto fin = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
        const float rec = fin(p.recover, 0.f, 1.f, 0.f), tam = fin(p.tame, 0.f, 1.f, 0.f);
        const float bia = fin(p.bias, -1.f, 1.f, 0.f),   bri = fin(p.brighten, -1.f, 1.f, 0.f);
        const float amt = fin(p.amount, 0.f, 1.f, 1.f),  bst = fin(p.boostDb, -24.f, 24.f, 0.f);
        float lo = fin(p.lowHz, 10.f, 30000.f, 20.f), hi = fin(p.highHz, 10.f, 30000.f, 20000.f);
        if (lo > hi) std::swap(lo, hi);                       // FIX: crossed limits
        const float l2lo = std::log2(lo), l2hi = std::log2(hi);
        const float byp = p.bypass ? 1.0f : 0.0f, dlt = p.delta ? 1.0f : 0.0f;

        if (! paramsInitialised) {                             // FIX: no fade-in from 0
            sRecover.setCurrentAndTarget(rec); sTame.setCurrentAndTarget(tam);
            sBias.setCurrentAndTarget(bia);    sBrighten.setCurrentAndTarget(bri);
            sAmount.setCurrentAndTarget(amt);  sBoost.setCurrentAndTarget(bst);
            sLow.setCurrentAndTarget(l2lo);    sHigh.setCurrentAndTarget(l2hi);
            sBypass.setCurrentAndTarget(byp);  sDelta.setCurrentAndTarget(dlt);
            paramsInitialised = true;
        } else {
            sRecover.setTarget(rec); sTame.setTarget(tam); sBias.setTarget(bia); sBrighten.setTarget(bri);
            sAmount.setTarget(amt);  sBoost.setTarget(bst); sLow.setTarget(l2lo); sHigh.setTarget(l2hi);
            sBypass.setTarget(byp);  sDelta.setTarget(dlt);
        }
        gainMatchOn = p.gainMatch;
        clipOn = p.safetyClip;
    }

    // In-place processing. 1 or 2 channels; any block size (including 0).
    void process(float* const* ch, int numChannels, int numSamples) noexcept {
        if (numSamples <= 0 || ch == nullptr) return;
        const int nc = std::clamp(numChannels, 0, 2);
        if (nc == 0) return;
        float* L = ch[0];
        float* R = nc > 1 ? ch[1] : nullptr;

        int pos = 0;
        while (pos < numSamples) {
            const int len = std::min(numSamples - pos, ctrlN - ctrlCount);
            processChunk(L + pos, R ? R + pos : nullptr, len);
            pos += len;
            ctrlCount += len;
            if (ctrlCount >= ctrlN) { controlUpdate(); ctrlCount = 0; }
        }

        // publish meters: accumulate max until the GUI takes it (takePeaks)
        if (! R) { peakIn[1] = peakIn[0]; peakOut[1] = peakOut[0]; }
        for (int c = 0; c < 2; ++c) {
            if (peakIn[c]  > snap.inPeak[c].load(std::memory_order_relaxed))  snap.inPeak[c].store(peakIn[c], std::memory_order_relaxed);
            if (peakOut[c] > snap.outPeak[c].load(std::memory_order_relaxed)) snap.outPeak[c].store(peakOut[c], std::memory_order_relaxed);
        }
        peakIn[0] = peakIn[1] = peakOut[0] = peakOut[1] = 0.0f;
        snap.counter.fetch_add(1, std::memory_order_release);
    }

    const Snapshot& getSnapshot() const noexcept { return snap; }
    // GUI thread: read-and-reset peak meters (benign race: a lost reset only shows a peak twice)
    void takePeaks(float in[2], float out[2]) const noexcept {
        for (int c = 0; c < 2; ++c) {
            in[c]  = snap.inPeak[c].exchange(0.0f, std::memory_order_relaxed);
            out[c] = snap.outPeak[c].exchange(0.0f, std::memory_order_relaxed);
        }
    }
    double getSampleRate() const noexcept { return fs; }
    int getControlBlock() const noexcept { return ctrlN; }
    float getBandFreq(int b) const noexcept { return bandFreq[b]; }
    float getBandGainDb(int b) const noexcept { return gainSmoothed[b]; }
    float getBandTargetDb(int b) const noexcept { return gainTarget[b]; }
    float getBandLevelDb(int b) const noexcept { return levelDb[b]; }
    float getBellFreq(int i) const noexcept { return bellFreq[i]; }
    float getBellGainDb(int i) const noexcept { return appliedBellDb[i]; }
    double getBellQ() const noexcept { return bellQ; }
    float getGainMatchDb() const noexcept { return gmDb; }
    const KalmanEnvelope& getKalman(int b) const noexcept { return kalman[b]; }

    // Total applied EQ response (dB) at f — what the GUI draws.
    float responseDb(double f) const noexcept {
        float s = 0.0f;
        for (int i = 0; i < NUM_BELLS; ++i) s += bellMagnitudeDb(f, bellFreq[i], bellQ, appliedBellDb[i], fs);
        return s;
    }

private:
    static constexpr float  INPUT_CEILING = 1000.0f;        // +60 dBFS
    static constexpr float  RECOVER_SLOPE = 1.0f;           // dB of lift per dB masked
    static constexpr float  TAME_SLOPE    = 1.2f;           // dB of cut per dB of prominence
    static constexpr float  TAME_DEADZONE = 1.0f;           // dB of prominence ignored
    static constexpr double SLOPE_UP_DB_PER_BARK   = 12.0;  // masker spreading toward higher f
    static constexpr double SLOPE_DOWN_DB_PER_BARK = 25.0;  // masker spreading toward lower f

    // ---------------------------------------------------------------------------
    void processChunk(float* L, float* R, int n) noexcept {
        float dryL[MAX_CTRL], dryR[MAX_CTRL], mid[MAX_CTRL];

        // 1) sanitize + dry copy + mid (analysis input) ------------------------
        for (int i = 0; i < n; ++i) {
            const float l = sanitize(L[i]);
            L[i] = l; dryL[i] = l;
            float r = l;
            if (R) { r = sanitize(R[i]); R[i] = r; dryR[i] = r; }
            peakIn[0] = std::max(peakIn[0], std::abs(l));
            if (R) peakIn[1] = std::max(peakIn[1], std::abs(r));
            const float m = 0.5f * (l + r);
            const float y = m - dcX1 + dcR * dcY1;                    // DC blocker (analysis only)
            dcX1 = m; dcY1 = y;
            mid[i] = y;
            inMsAcc += std::min(0.5f * (l * l + r * r), 1.0e30f);
        }

        // 2) analysis bank: 32 TPT band-passes, band-inner loop (vectorises) --
        for (int i = 0; i < n; ++i) {
            const float x = mid[i];
            for (int b = 0; b < NUM_BANDS; ++b) {
                const float v3 = x - bic2[b];
                const float v1 = ba1[b] * bic1[b] + ba2[b] * v3;
                const float v2 = bic2[b] + ba2[b] * bic1[b] + ba3[b] * v3;
                bic1[b] = 2.0f * v1 - bic1[b];
                bic2[b] = 2.0f * v2 - bic2[b];
                acc[b] += v1 * v1;
            }
        }

        // 3) bell cascade, L and R interleaved for ILP --------------------------
        for (int k = 0; k < NUM_BELLS; ++k) {
            const float m1 = em1[k];
            if (exactly(m1, 0.0f)) continue;                // exact unity: skip (keeps null)
            const float a1 = ea1[k], a2 = ea2[k], a3 = ea3[k];
            float l1 = eic1[0][k], l2 = eic2[0][k];
            if (R) {
                float r1 = eic1[1][k], r2 = eic2[1][k];
                for (int i = 0; i < n; ++i) {
                    const float xl = L[i], xr = R[i];
                    const float lv3 = xl - l2, rv3 = xr - r2;
                    const float lv1 = a1 * l1 + a2 * lv3, rv1 = a1 * r1 + a2 * rv3;
                    const float lv2 = l2 + a2 * l1 + a3 * lv3, rv2 = r2 + a2 * r1 + a3 * rv3;
                    l1 = 2.0f * lv1 - l1; l2 = 2.0f * lv2 - l2;
                    r1 = 2.0f * rv1 - r1; r2 = 2.0f * rv2 - r2;
                    L[i] = xl + m1 * lv1;
                    R[i] = xr + m1 * rv1;
                }
                eic1[1][k] = r1; eic2[1][k] = r2;
            } else {
                for (int i = 0; i < n; ++i) {
                    const float xl = L[i];
                    const float lv3 = xl - l2;
                    const float lv1 = a1 * l1 + a2 * lv3;
                    const float lv2 = l2 + a2 * l1 + a3 * lv3;
                    l1 = 2.0f * lv1 - l1; l2 = 2.0f * lv2 - l2;
                    L[i] = xl + m1 * lv1;
                }
            }
            eic1[0][k] = l1; eic2[0][k] = l2;
        }

        // 4) output stage ------------------------------------------------------
        for (int i = 0; i < n; ++i) {
            const float boost = dbToGainFast(sBoost.next());
            const float byp = sBypass.next();
            const float dlt = sDelta.next();
            gmGainCur += gmStep;
            const float gm = gmGainCur;

            float l = L[i];
            float r = R ? R[i] : 0.0f;
            const float ol = std::min(l * l, 1.0e30f), orr = R ? std::min(r * r, 1.0e30f) : ol;
            outMsAcc += 0.5f * (ol + orr);

            // wet = gain-matched EQ, delta = what the EQ changed
            float wl = gm * l, wr = gm * r;
            wl -= dlt * dryL[i];                      // dlt=1 -> wet - dry
            if (R) wr -= dlt * dryR[i];
            wl *= boost; wr *= boost;

            // bypass crossfade (latency is zero, so dry is aligned)
            float yl = wl + byp * (dryL[i] - wl);
            float yr = R ? wr + byp * (dryR[i] - wr) : 0.0f;
            if (clipOn) { yl = transparentSoftClip(yl); yr = transparentSoftClip(yr); }

            L[i] = yl;
            peakOut[0] = std::max(peakOut[0], std::abs(yl));
            if (R) { R[i] = yr; peakOut[1] = std::max(peakOut[1], std::abs(yr)); }
        }
    }

    // NaN/inf -> 0, |x| > +60 dBFS -> clamped (garbage guard), denormal -> 0 (DAZ)
    static float sanitize(float x) noexcept {
        if (! std::isfinite(x)) return 0.0f;
        const float a = std::abs(x);
        if (a < 1.0e-30f) return 0.0f;
        return a > INPUT_CEILING ? (x > 0.0f ? INPUT_CEILING : -INPUT_CEILING) : x;
    }

    static float dbToGainFast(float db) noexcept {
        return exactly(db, 0.0f) ? 1.0f : std::exp(db * 0.11512925464970229f);
    }

    // Masking from OTHER bands only, O(N): forward (upward spread) + backward pass.
    void othersMasking(const float* E, float* Mo) const noexcept {
        float F[NUM_BANDS], B[NUM_BANDS];
        F[0] = E[0];
        for (int b = 1; b < NUM_BANDS; ++b) F[b] = E[b] + aUp[b] * F[b - 1];
        B[NUM_BANDS - 1] = E[NUM_BANDS - 1];
        for (int b = NUM_BANDS - 2; b >= 0; --b) B[b] = E[b] + aDn[b] * B[b + 1];
        for (int b = 0; b < NUM_BANDS; ++b) {
            const float up = b > 0 ? aUp[b] * F[b - 1] : 0.0f;
            const float dn = b < NUM_BANDS - 1 ? aDn[b] * B[b + 1] : 0.0f;
            Mo[b] = up + dn;
        }
    }

    // ---------------------------------------------------------------------------
    void controlUpdate() noexcept {
        const float invN = 1.0f / static_cast<float>(ctrlN);
        const float recover  = sRecover.skip(1);
        const float tame     = sTame.skip(1);
        const float bias     = sBias.skip(1);
        const float brighten = sBrighten.skip(1);
        const float amount   = sAmount.skip(1);
        const float l2lo     = sLow.skip(1);
        const float l2hi     = sHigh.skip(1);

        // --- 0) filter health: NaN/inf recovery + denormal flush --------------
        bool bad = false;
        for (int b = 0; b < NUM_BANDS; ++b) {
            if (! std::isfinite(bic1[b]) || ! std::isfinite(bic2[b]) || ! std::isfinite(acc[b])) bad = true;
            if (std::abs(bic1[b]) < 1.0e-15f) bic1[b] = 0.0f;
            if (std::abs(bic2[b]) < 1.0e-15f) bic2[b] = 0.0f;
        }
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < NUM_BELLS; ++k) {
                if (! std::isfinite(eic1[c][k]) || ! std::isfinite(eic2[c][k])) bad = true;
                if (std::abs(eic1[c][k]) < 1.0e-15f) eic1[c][k] = 0.0f;
                if (std::abs(eic2[c][k]) < 1.0e-15f) eic2[c][k] = 0.0f;
            }
        if (std::abs(dcY1) < 1.0e-15f) dcY1 = 0.0f;
        if (! std::isfinite(dcY1) || ! std::isfinite(dcX1)) bad = true;
        if (bad) {
            std::fill(std::begin(bic1), std::end(bic1), 0.0f);
            std::fill(std::begin(bic2), std::end(bic2), 0.0f);
            std::fill(std::begin(acc), std::end(acc), 0.0f);
            for (int c = 0; c < 2; ++c) for (int k = 0; k < NUM_BELLS; ++k) eic1[c][k] = eic2[c][k] = 0.0f;
            dcX1 = dcY1 = 0.0f;
        }

        // --- 1) band levels -> Kalman (dB) ------------------------------------
        float Lp[NUM_BANDS];
        float totalPow = 0.0f;
        for (int b = 0; b < NUM_BANDS; ++b) {
            const float ms = acc[b] * bandNorm[b] * invN;
            acc[b] = 0.0f;
            totalPow += ms;
            const float db = std::clamp(10.0f * std::log10(ms + 1.0e-20f) + 3.0103f, -150.0f, 60.0f); // full-scale sine = 0 dBFS
            levelDb[b] = kalman[b].process(db);
        }

        // --- 2) perceptual levels ---------------------------------------------
        // 0 dBFS full-scale sine ~ 93 dB SPL (assumed monitoring reference)
        for (int b = 0; b < NUM_BANDS; ++b)
            Lp[b] = levelDb[b] + 93.0f + weightDb[b] - brighten * 6.0f * tiltShape[b];

        // --- 3) masking: two-pass spreading, normalised to a flat spectrum -----
        float E[NUM_BANDS], Mo[NUM_BANDS], dev[NUM_BANDS], prom[NUM_BANDS];
        for (int b = 0; b < NUM_BANDS; ++b) E[b] = std::pow(10.0f, (Lp[b] - 100.0f) * 0.1f);
        othersMasking(E, Mo);
        for (int b = 0; b < NUM_BANDS; ++b) {
            const float mr = (Lp[b] - 100.0f) - 10.0f * std::log10(Mo[b] + 1.0e-30f);
            dev[b] = mr - mr0[b];                         // <0: masked by neighbours
        }
        // local prominence vs a 7-band triangular envelope (linear slopes -> 0)
        for (int b = 0; b < NUM_BANDS; ++b) {
            float s = 0.0f, w = 0.0f;
            for (int j = -3; j <= 3; ++j) {
                const int i = b + j;
                if (i < 0 || i >= NUM_BANDS || j == 0) continue;
                const float wt = std::abs(j) == 1 ? 1.0f : 2.0f;   // de-weight leaky neighbours
                s += wt * Lp[i]; w += wt;
            }
            // edge bands: mirror the slope so a linear spectrum still reads 0
            prom[b] = Lp[b] - s / w;
            if (b < 3 || b > NUM_BANDS - 4) prom[b] *= 0.5f;
        }

        // --- 4) gain law --------------------------------------------------------
        const float recW = recover * std::clamp(1.0f + bias, 0.0f, 2.0f);
        const float tamW = tame    * std::clamp(1.0f - bias, 0.0f, 2.0f);
        const bool gateOpen = totalPow > 1.0e-9f;   // ~ -90 dBFS total
        float raw[NUM_BANDS];
        for (int b = 0; b < NUM_BANDS; ++b) {
            const float masked = std::max(0.0f, -dev[b]) * RECOVER_SLOPE;
            const float hot    = std::max(0.0f, prom[b] - TAME_DEADZONE) * TAME_SLOPE;
            const float gRec = recW * softLimit(masked, MAX_GAIN_DB);
            const float gTam = -tamW * softLimit(hot, MAX_GAIN_DB);
            // audibility: don't lift what can't be heard (noise floor, ultrasonics)
            const float splB = levelDb[b] + 93.0f;
            const float aud = gateOpen ? std::clamp((splB - thresholdDb[b]) / 15.0f, 0.0f, 1.0f) : 0.0f;
            raw[b] = gRec * aud + gTam * (gateOpen ? 1.0f : 0.0f);
        }
        // smooth across bands so the curve stays musical
        for (int b = 0; b < NUM_BANDS; ++b) {
            const float lo = raw[std::max(0, b - 1)], hi = raw[std::min(NUM_BANDS - 1, b + 1)];
            float g = 0.2f * lo + 0.6f * raw[b] + 0.2f * hi;
            g += brighten * 3.0f * tiltShape[b];                       // static tilt part
            g = std::clamp(g, -MAX_GAIN_DB, MAX_GAIN_DB) * amount;
            // frequency limits: 1/3-octave smoothstep taper
            const float tl = smoothstep((log2Freq[b] - l2lo) * 3.0f + 0.5f);
            const float th = smoothstep((l2hi - log2Freq[b]) * 3.0f + 0.5f);
            g *= tl * th;
            if (! std::isfinite(g)) g = 0.0f;
            gainTarget[b] = g;
            float gs = gainSmoothed[b] + gainSmoothCoeff * (g - gainSmoothed[b]);
            if (std::abs(gs - g) < 1.0e-4f) gs = g;                    // snap -> exact 0 dB possible
            gainSmoothed[b] = gs;
        }

        // --- 5) bell coefficients ---------------------------------------------
        for (int k = 0; k < NUM_BELLS; ++k) {
            const float ga = gainSmoothed[2 * k], gb = gainSmoothed[2 * k + 1];
            const float pair = (ga < 0.0f && gb < 0.0f) ? 0.35f * (ga + gb) + 0.3f * std::min(ga, gb) : 0.5f * (ga + gb);
            const float gdb = std::clamp(bellComp * pair,
                                         -MAX_GAIN_DB * 1.25f, MAX_GAIN_DB * 1.25f);
            appliedBellDb[k] = gdb;
            if (exactly(gdb, 0.0f)) { em1[k] = 0.0f; bellActive[k] = false; continue; }
            if (! bellActive[k]) {                       // waking up: start from clean state
                eic1[0][k] = eic2[0][k] = eic1[1][k] = eic2[1][k] = 0.0f;
                bellActive[k] = true;
            }
            const float A = std::pow(10.0f, gdb / 40.0f);
            const float kq = 1.0f / (static_cast<float>(bellQ) * A);
            const float g = bellG[k];
            const float a1 = 1.0f / (1.0f + g * (g + kq));
            ea1[k] = a1; ea2[k] = g * a1; ea3[k] = g * g * a1;
            em1[k] = kq * (A * A - 1.0f);
        }

        // --- 6) gain match (measures pre-match wet, so no feedback loop) ------
        const float inMs = inMsAcc * invN, outMs = outMsAcc * invN;
        inMsAcc = outMsAcc = 0.0f;
        gmInPow  += gmPowCoeff * (std::min(inMs, 1.0e12f)  - gmInPow);
        gmOutPow += gmPowCoeff * (std::min(outMs, 1.0e12f) - gmOutPow);
        float gmTarget = 0.0f;
        if (gainMatchOn && gmInPow > 1.0e-7f && gmOutPow > 1.0e-12f)   // gate below ~-70 dBFS
            gmTarget = std::clamp(10.0f * std::log10(gmInPow / gmOutPow), -MAX_GAIN_DB, MAX_GAIN_DB);
        else if (gainMatchOn)
            gmTarget = gmDb;                                           // hold in silence
        gmDb += gmGainCoeff * (gmTarget - gmDb);
        if (std::abs(gmDb) < 1.0e-4f && exactly(gmTarget, 0.0f)) gmDb = 0.0f;
        gmGainNext = exactly(gmDb, 0.0f) ? 1.0f : std::pow(10.0f, gmDb * 0.05f);
        gmStep = (gmGainNext - gmGainCur) * invN;
        if (exactly(gmGainNext, 1.0f) && std::abs(gmGainCur - 1.0f) < 1.0e-6f) { gmGainCur = 1.0f; gmStep = 0.0f; }

        // --- 7) publish to GUI ----------------------------------------------------
        for (int b = 0; b < NUM_BANDS; ++b) {
            snap.bandLevelDb[(size_t) b].store(levelDb[b], std::memory_order_relaxed);
            snap.bandGainDb[(size_t) b].store(gainSmoothed[b], std::memory_order_relaxed);
        }
        for (int k = 0; k < NUM_BELLS; ++k) snap.bellGainDb[(size_t) k].store(appliedBellDb[k], std::memory_order_relaxed);
        snap.gainMatchDb.store(gmDb, std::memory_order_relaxed);
        snap.inRmsDb.store(10.0f * std::log10(gmInPow + 1.0e-12f), std::memory_order_relaxed);
        snap.outRmsDb.store(10.0f * std::log10(gmOutPow + 1.0e-12f), std::memory_order_relaxed);
    }

    static float softLimit(float x, float m) noexcept { return m * std::tanh(x / m); }   // x>=0
    static float smoothstep(float t) noexcept { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

    // ---- state -------------------------------------------------------------------
    double fs = 48000.0, ctrlRate = 1500.0, fTop = 20000.0, fBot = 20.0, bellQ = 2.0;
    int ctrlN = 32, ctrlCount = 0;
    bool paramsInitialised = false, gainMatchOn = false, clipOn = false;

    alignas(32) float ba1[NUM_BANDS] {}, ba2[NUM_BANDS] {}, ba3[NUM_BANDS] {};
    alignas(32) float bic1[NUM_BANDS] {}, bic2[NUM_BANDS] {}, acc[NUM_BANDS] {};
    float bandNorm[NUM_BANDS] {}, bandFreq[NUM_BANDS] {}, log2Freq[NUM_BANDS] {};
    float weightDb[NUM_BANDS] {}, thresholdDb[NUM_BANDS] {}, tiltShape[NUM_BANDS] {}, mr0[NUM_BANDS] {};
    float aUp[NUM_BANDS] {}, aDn[NUM_BANDS] {};
    float levelDb[NUM_BANDS] {}, gainTarget[NUM_BANDS] {}, gainSmoothed[NUM_BANDS] {};
    KalmanEnvelope kalman[NUM_BANDS];

    float bellFreq[NUM_BELLS] {}, bellG[NUM_BELLS] {}, appliedBellDb[NUM_BELLS] {};
    float ea1[NUM_BELLS] {}, ea2[NUM_BELLS] {}, ea3[NUM_BELLS] {}, em1[NUM_BELLS] {};
    float eic1[2][NUM_BELLS] {}, eic2[2][NUM_BELLS] {};
    float bellComp = 1.0f;
    bool  bellActive[NUM_BELLS] {};

    float dcR = 0.999f, dcX1 = 0.0f, dcY1 = 0.0f;
    float gainSmoothCoeff = 0.02f;

    float inMsAcc = 0.0f, outMsAcc = 0.0f, gmInPow = 1.0e-12f, gmOutPow = 1.0e-12f;
    float gmPowCoeff = 0.001f, gmGainCoeff = 0.002f, gmDb = 0.0f;
    float gmGainCur = 1.0f, gmGainNext = 1.0f, gmStep = 0.0f;

    float peakIn[2] {}, peakOut[2] {};

    LinearSmoother sRecover, sTame, sBias, sBrighten, sAmount, sLow, sHigh;  // control rate
    LinearSmoother sBoost, sBypass, sDelta;                                  // audio rate

    mutable Snapshot snap;
};

} // namespace wavesurfer
