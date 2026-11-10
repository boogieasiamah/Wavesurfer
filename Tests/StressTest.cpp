// WaveSurfer — extreme-level quality check harness (no JUCE needed)
// Build:  g++ -std=c++17 -O2 -I../Source Tests/StressTest.cpp -o stress && ./stress
#include "dsp/WaveSurferDSP.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>
#include <algorithm>

using namespace wavesurfer;
using Clock = std::chrono::high_resolution_clock;

static int g_pass = 0, g_fail = 0;
static std::vector<std::string> g_failures;
#define CHECK(cond, ...) do { if (cond) ++g_pass; else { ++g_fail; char b_[512]; std::snprintf(b_, sizeof b_, __VA_ARGS__); \
    g_failures.push_back(b_); std::printf("    FAIL: %s\n", b_); } } while (0)

// ---------------------------------------------------------------- signals ----
struct Rng { uint64_t s = 0x9E3779B97F4A7C15ull; float next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (float)((s >> 11) * (1.0 / 9007199254740992.0)) * 2.0f - 1.0f; } };

struct Pink { // Paul Kellet's refined pink filter
    float b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0; Rng r;
    float next() { float w = r.next(); b0=0.99886f*b0+w*0.0555179f; b1=0.99332f*b1+w*0.0750759f; b2=0.96900f*b2+w*0.1538520f;
        b3=0.86650f*b3+w*0.3104856f; b4=0.55000f*b4+w*0.5329522f; b5=-0.7616f*b5-w*0.0168980f; float p=b0+b1+b2+b3+b4+b5+b6+w*0.5362f; b6=w*0.115926f; return p*0.11f; }
};

enum class Sig { Pink, White, Sine1k, Sine20, SineHigh, Impulse, Silence, DC, Square, Hot, Tiny, Denormal, PinkPlusTone };
static const char* sigName(Sig s) { switch (s) { case Sig::Pink: return "pink"; case Sig::White: return "white"; case Sig::Sine1k: return "sine1k0dB";
    case Sig::Sine20: return "sine20Hz"; case Sig::SineHigh: return "sine0.45fs"; case Sig::Impulse: return "impulses"; case Sig::Silence: return "silence";
    case Sig::DC: return "DC"; case Sig::Square: return "square0dB"; case Sig::Hot: return "noise+24dB"; case Sig::Tiny: return "noise-120dB";
    case Sig::Denormal: return "denormal"; case Sig::PinkPlusTone: return "pink+2k"; } return "?"; }

static void gen(Sig s, double fs, int n, std::vector<float>& L, std::vector<float>& R, uint64_t seed = 1) {
    L.resize(n); R.resize(n); Pink pl, pr; pl.r.s ^= seed * 7919; pr.r.s ^= seed * 104729 + 3; Rng w; w.s ^= seed;
    const double tw = 2.0 * 3.14159265358979;
    for (int i = 0; i < n; ++i) {
        float l = 0, r = 0;
        switch (s) {
            case Sig::Pink: l = 0.5f * pl.next(); r = 0.5f * pr.next(); break;
            case Sig::White: l = 0.5f * w.next(); r = 0.5f * w.next(); break;
            case Sig::Sine1k: l = r = (float)std::sin(tw * 1000.0 * i / fs); break;
            case Sig::Sine20: l = r = (float)std::sin(tw * 20.0 * i / fs); break;
            case Sig::SineHigh: l = r = 0.9f * (float)std::sin(tw * 0.45 * fs * i / fs); break;
            case Sig::Impulse: l = r = (i % 4800 == 0) ? 1.0f : 0.0f; break;
            case Sig::Silence: break;
            case Sig::DC: l = r = 0.5f; break;
            case Sig::Square: l = r = (std::fmod(i * 100.0 / fs, 1.0) < 0.5) ? 1.0f : -1.0f; break;
            case Sig::Hot: l = 15.85f * w.next(); r = 15.85f * w.next(); break;
            case Sig::Tiny: l = 1e-6f * w.next(); r = 1e-6f * w.next(); break;
            case Sig::Denormal: l = 1e-39f * w.next(); r = 1e-39f * w.next(); break;
            case Sig::PinkPlusTone: l = 0.25f * pl.next() + 0.25f * (float)std::sin(tw * 2000.0 * i / fs); r = 0.25f * pr.next() + 0.25f * (float)std::sin(tw * 2000.0 * i / fs); break;
        }
        L[i] = l; R[i] = r;
    }
}

// run engine over buffers in blocks (blockSize<=0 -> random 1..2048)
static void run(WaveSurferEngine& e, std::vector<float>& L, std::vector<float>& R, int blockSize, bool stereo = true,
                std::function<void(int)> perBlock = nullptr, uint64_t seed = 42) {
    Rng r; r.s ^= seed; int pos = 0, n = (int)L.size(), blk = 0;
    while (pos < n) {
        int bs = blockSize > 0 ? blockSize : 1 + (int)((r.next() * 0.5f + 0.5f) * 2047.0f);
        bs = std::min(bs, n - pos);
        if (perBlock) perBlock(blk);
        float* ch[2] = { L.data() + pos, R.data() + pos };
        e.process(ch, stereo ? 2 : 1, bs);
        pos += bs; ++blk;
    }
}

static float peak(const std::vector<float>& v, int from = 0) { float p = 0; for (size_t i = from; i < v.size(); ++i) p = std::max(p, std::abs(v[i])); return p; }
static bool allFinite(const std::vector<float>& v) { for (float x : v) if (!std::isfinite(x)) return false; return true; }
static double rmsDb(const std::vector<float>& v, int from = 0) { double s = 0; for (size_t i = from; i < v.size(); ++i) s += (double)v[i] * v[i]; return 10 * std::log10(s / std::max<size_t>(1, v.size() - from) + 1e-30); }
static float maxDiff(const std::vector<float>& v, int from = 0) { float m = 0; for (size_t i = std::max(1, from); i < v.size(); ++i) m = std::max(m, std::abs(v[i] - v[i - 1])); return m; }

static Params P(float rec, float tam, float bias, float bri, float boost, float amt, float lo = 20, float hi = 20000) {
    Params p; p.recover = rec; p.tame = tam; p.bias = bias; p.brighten = bri; p.boostDb = boost; p.amount = amt; p.lowHz = lo; p.highHz = hi; return p;
}

// ================================================================== tests ====
static void testClipper() {
    std::puts("[T01] soft clipper: unity, continuity, monotonic, ceiling");
    bool unity = true, mono = true, ceil_ = true; float prev = -2;
    for (int i = -20000; i <= 20000; ++i) { float x = i * 1e-4f; float y = transparentSoftClip(x);
        if (std::abs(x) <= 0.9f && y != x) unity = false;
        if (y < prev - 1e-7f) mono = false;
        prev = y;
        if (std::abs(y) > 1.0f) ceil_ = false; }
    CHECK(unity, "clipper not unity below 0.9"); CHECK(mono, "clipper not monotonic"); CHECK(ceil_, "clipper exceeds 1.0");
    CHECK(std::abs(transparentSoftClip(1.2f) - 1.0f) < 1e-6f, "clipper(1.2) != 1");
    float d = (transparentSoftClip(0.9001f) - transparentSoftClip(0.8999f)) / 0.0002f;
    CHECK(std::abs(d - 1.0f) < 0.01f, "clipper slope at knee = %f", d);
    CHECK(transparentSoftClip(1e30f) == 1.0f && transparentSoftClip(-1e30f) == -1.0f, "clipper extreme input");
}

static void testKalman() {
    std::puts("[T02] Kalman: time constant identical across rates (old 1/fs scaling drifted)");
    double tauMeasured[3]; double rates[3] = { 44100.0 / 29, 96000.0 / 64, 384000.0 / 256 };
    for (int k = 0; k < 3; ++k) {
        KalmanEnvelope ke; ke.prepare(rates[k], 0.02); ke.reset(0.0f);
        for (int i = 0; i < 2000; ++i) ke.process(0.0f);               // converge P
        int n = 0; while (ke.process(1.0f) < 0.632f && n < 100000) ++n;
        tauMeasured[k] = (n + 1) / rates[k];
        double Kss = 1.0 - std::exp(-1.0 / (0.02 * rates[k]));
        CHECK(std::abs(ke.steadyStateGain() - Kss) / Kss < 1e-3, "steady-state K %g vs %g", ke.steadyStateGain(), Kss);
    }
    std::printf("    tau @ctrl rates: %.2f / %.2f / %.2f ms (target 20)\n", tauMeasured[0] * 1e3, tauMeasured[1] * 1e3, tauMeasured[2] * 1e3);
    for (int k = 0; k < 3; ++k) CHECK(std::abs(tauMeasured[k] - 0.02) < 0.0015, "tau %.3f ms", tauMeasured[k] * 1e3);
    // old formula for the record
    auto oldTau = [](double fs) { double Q = 0.001 * 44100 / fs, R = 0.05; double M = 0.5 * (Q + std::sqrt(Q * Q + 4 * Q * R)); double K = M / (M + R); return -1.0 / std::log(1 - K) / fs; };
    std::printf("    (old code: %.3f ms @44.1k vs %.3f ms @96k)\n", oldTau(44100) * 1e3, oldTau(96000) * 1e3);
    KalmanEnvelope ke; ke.prepare(1500, 0.02); ke.reset(-20); float before = ke.process(std::numeric_limits<float>::quiet_NaN());
    CHECK(std::isfinite(before) && before == -20.0f, "Kalman NaN guard");
}

static void testIso() {
    std::puts("[T03] ISO 226 + bell magnitude formula vs simulated filter");
    double l1k = iso226SplDb(1000, 70), l100 = iso226SplDb(100, 70), l3k = iso226SplDb(3150, 70);
    std::printf("    70 phon: 100Hz %.1f, 1k %.1f, 3.15k %.1f dB SPL\n", l100, l1k, l3k);
    CHECK(std::abs(l1k - 70) < 0.6, "ISO226 1k@70 = %.2f", l1k); CHECK(l100 > l1k + 5, "ISO226 low end"); CHECK(l3k < l1k, "ISO226 3k dip");
    // simulate one bell and compare to the closed form used by the GUI
    double fs = 48000, fc = 1000, q = 2.15, gdb = 9;
    double A = std::pow(10, gdb / 40), kq = 1 / (q * A), g = std::tan(3.14159265358979323846 * fc / fs), a1 = 1 / (1 + g * (g + kq)), a2 = g * a1, a3 = g * a2, m1 = kq * (A * A - 1);
    double worst = 0;
    for (double f : { 50.0, 300.0, 800.0, 1000.0, 1300.0, 5000.0, 18000.0 }) {
        double ic1 = 0, ic2 = 0, pk = 0; int N = (int)(fs * 0.5);
        for (int i = 0; i < N; ++i) { double x = std::sin(2 * 3.14159265358979323846 * f * i / fs); double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; double y = x + m1 * v1; if (i > N / 2) pk = std::max(pk, std::abs(y)); }
        double meas = 20 * std::log10(pk), form = bellMagnitudeDb(f, fc, q, gdb, fs);
        worst = std::max(worst, std::abs(meas - form));
    }
    std::printf("    GUI curve formula max error: %.3f dB\n", worst);
    CHECK(worst < 0.05, "bell formula error %.3f dB", worst);
    CHECK(std::abs(bellMagnitudeDb(1000, 1000, q, 9, fs) - 9) < 0.01, "bell centre gain");
    CHECK(std::abs(bellMagnitudeDb(1000, 1000, q, 9, fs) + bellMagnitudeDb(1000, 1000, q, -9, fs)) < 0.01, "bell boost/cut symmetry");
}

static void testNull() {
    std::puts("[T04] bit-exact null: defaults, and Amount=0 with everything else maxed");
    for (int pass = 0; pass < 2; ++pass) {
        WaveSurferEngine e; e.prepare(48000); std::vector<float> L, R; gen(Sig::Pink, 48000, 48000 * 3, L, R);
        auto L0 = L, R0 = R;
        Params p = pass == 0 ? Params{} : P(1, 1, 1, 1, 0, 0, 20, 20000);
        if (pass == 1) { e.setParameters(P(1, 1, 1, 1, 0, 1)); std::vector<float> a = L, b = R; run(e, a, b, 512); } // get it busy first
        e.setParameters(p);
        run(e, L, R, 0);
        int from = 48000 * 2; bool exact = true;
        for (size_t i = from; i < L.size(); ++i) if (L[i] != L0[i] || R[i] != R0[i]) { exact = false; break; }
        CHECK(exact, "%s not bit-exact", pass == 0 ? "defaults" : "amount=0");
    }
}

static void testExtremeGrid() {
    std::puts("[T05] all 256 parameter corners x 6 signals x random toggles (finite, bounded, clip ceiling)");
    const float corners[8][2] = { {0,1}, {0,1}, {-1,1}, {-1,1}, {-12,12}, {0,1}, {20,2000}, {1000,20000} };
    const Sig sigs[] = { Sig::Pink, Sig::Sine1k, Sig::Hot, Sig::Square, Sig::Impulse, Sig::PinkPlusTone };
    int runs = 0, bad = 0; float worstGainDb = -999; Rng tog;
    for (int c = 0; c < 256; ++c)
        for (Sig s : sigs) {
            WaveSurferEngine e; e.prepare(48000);
            Params p; float v[8]; for (int k = 0; k < 8; ++k) v[k] = corners[k][(c >> k) & 1];
            p = P(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
            p.gainMatch = tog.next() > 0; p.delta = tog.next() > 0.6f; p.safetyClip = tog.next() > 0;
            e.setParameters(p);
            std::vector<float> L, R; gen(s, 48000, 24000, L, R, c + 1); float inPk = std::max(peak(L), peak(R));
            run(e, L, R, 0, true, nullptr, c);
            ++runs;
            bool ok = allFinite(L) && allFinite(R);
            float outPk = std::max(peak(L), peak(R));
            if (p.safetyClip && outPk > 1.0f) ok = false;
            float gdb = 20 * std::log10((outPk + 1e-12f) / (inPk + 1e-12f));
            if (!p.delta) worstGainDb = std::max(worstGainDb, gdb);
            if (gdb > 45.0f) ok = false;   // +15 EQ +12 match +12 boost + overlap margin
            if (!ok) { ++bad; if (bad < 6) std::printf("    bad corner %d sig %s outPk %g\n", c, sigName(s), outPk); }
        }
    std::printf("    %d runs, worst peak gain %.1f dB\n", runs, worstGainDb);
    CHECK(bad == 0, "%d of %d extreme runs failed", bad, runs);
}

static void testNaN() {
    std::puts("[T06] NaN / +inf / -inf / 1e30 injection -> finite output, full recovery");
    WaveSurferEngine e; e.prepare(48000); e.setParameters(P(1, 1, 0, 0.5f, 6, 1));
    std::vector<float> L, R; gen(Sig::Pink, 48000, 48000 * 3, L, R);
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    L[1000] = nan; R[2000] = inf; L[3000] = -inf; R[4000] = 1e30f; L[4001] = -1e30f; R[5000] = std::numeric_limits<float>::denorm_min();
    for (int i = 6000; i < 6100; ++i) L[i] = nan;
    run(e, L, R, 256);
    CHECK(allFinite(L) && allFinite(R), "non-finite output after injection");
    WaveSurferEngine ref; ref.prepare(48000); ref.setParameters(P(1, 1, 0, 0.5f, 6, 1));
    std::vector<float> L2, R2; gen(Sig::Pink, 48000, 48000 * 3, L2, R2); run(ref, L2, R2, 256);
    double d = std::abs(rmsDb(L, 96000) - rmsDb(L2, 96000));
    std::printf("    last-second level vs clean run: %.2f dB apart\n", d);
    CHECK(d < 1.0, "did not recover after 1e30 hit (%.2f dB)", d);
}

static void testRatesBlocks() {
    std::puts("[T07] sample rates 22.05k..384k x block sizes 1..8192 (finite + block-size determinism)");
    const double rates[] = { 22050, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000 };
    const int blocks[] = { 1, 3, 32, 64, 441, 512, 4096, 8192, 0 };
    int mism = 0, nonfin = 0, total = 0;
    for (double fs : rates) {
        std::vector<float> refL, refR;
        for (int bs : blocks) {
            WaveSurferEngine e; e.prepare(fs, bs); e.setParameters(P(0.8f, 0.6f, 0.2f, 0.3f, 3, 1));
            std::vector<float> L, R; gen(Sig::PinkPlusTone, fs, (int)(fs * 0.6), L, R);
            run(e, L, R, bs); ++total;
            if (!allFinite(L) || !allFinite(R)) ++nonfin;
            if (refL.empty()) { refL = L; refR = R; }
            else if (std::memcmp(refL.data(), L.data(), L.size() * 4) || std::memcmp(refR.data(), R.data(), R.size() * 4)) ++mism;
        }
        WaveSurferEngine e; e.prepare(fs);
        std::printf("    %6.0f Hz: ctrl block %3d, top band %.0f Hz\n", fs, e.getControlBlock(), e.getBandFreq(31));
    }
    CHECK(nonfin == 0, "%d non-finite runs", nonfin);
    CHECK(mism == 0, "%d of %d runs differ with block size (should be bit-identical)", mism, total);
}

static void testAutomation() {
    std::puts("[T08] automation torture: random jumps every 64 samples + bypass/delta toggles (click check)");
    WaveSurferEngine e; e.prepare(48000);
    std::vector<float> L, R; gen(Sig::Sine1k, 48000, 48000 * 4, L, R); for (auto& x : L) x *= 0.5f; for (auto& x : R) x *= 0.5f;
    float dIn = maxDiff(L);
    Rng r;
    run(e, L, R, 64, true, [&](int) {
        Params p = P(r.next() * .5f + .5f, r.next() * .5f + .5f, r.next(), r.next(), r.next() * 12, r.next() * .5f + .5f,
                     20 + (r.next() * .5f + .5f) * 1980, 1000 + (r.next() * .5f + .5f) * 19000);
        p.bypass = r.next() > 0.8f; p.delta = false; p.gainMatch = r.next() > 0; e.setParameters(p); });
    float dOut = maxDiff(L, 4800);
    std::printf("    max step in %.4f, out %.4f (ratio %.2f; boost+EQ allow up to ~%.1fx)\n", dIn, dOut, dOut / dIn, std::pow(10.0, 27 / 20.0));
    CHECK(allFinite(L) && allFinite(R), "non-finite under automation");
    CHECK(dOut / dIn < std::pow(10.0, 27 / 20.0) * 1.2, "discontinuity under automation (ratio %.2f)", dOut / dIn);

    // bypass toggling on a steady sine at unity settings should be seamless
    WaveSurferEngine b; b.prepare(48000);
    std::vector<float> L2, R2; gen(Sig::Sine1k, 48000, 48000 * 2, L2, R2); for (auto& x : L2) x *= 0.5f; for (auto& x : R2) x *= 0.5f;
    float din = maxDiff(L2); bool by = false;
    run(b, L2, R2, 480, true, [&](int k) { Params p = P(1, 1, 0, 1, 0, 1); if (k % 10 == 0) by = !by; p.bypass = by; b.setParameters(p); });
    float dout = maxDiff(L2, 4800);
    std::printf("    bypass toggling: step ratio %.2f\n", dout / din);
    CHECK(dout / din < 4.0f, "bypass click (ratio %.2f)", dout / din);
}

static void testBehaviour() {
    std::puts("[T09] behaviour: Tame cuts a resonance, Recover lifts masked bands, limits respected");
    // Tame
    { WaveSurferEngine e; e.prepare(48000); e.setParameters(P(0, 1, 0, 0, 0, 1));
      std::vector<float> L, R; gen(Sig::PinkPlusTone, 48000, 48000 * 3, L, R); run(e, L, R, 512);
      int b2k = 0; for (int b = 0; b < 32; ++b) if (std::abs(std::log2(e.getBandFreq(b) / 2000)) < std::abs(std::log2(e.getBandFreq(b2k) / 2000))) b2k = b;
      std::printf("    Tame: gain @%.0f Hz = %.2f dB, response @2k = %.2f dB\n", e.getBandFreq(b2k), e.getBandGainDb(b2k), e.responseDb(2000));
      CHECK(e.responseDb(2000) < -3.0f, "Tame did not cut the 2 kHz resonance (%.2f dB)", e.responseDb(2000)); }
    // Recover on a dark (falling) spectrum
    { WaveSurferEngine e; e.prepare(48000); e.setParameters(P(1, 0, 0, 0, 0, 1));
      std::vector<float> L, R; gen(Sig::Pink, 48000, 48000 * 3, L, R);
      float y1 = 0, y2 = 0; for (size_t i = 0; i < L.size(); ++i) { y1 += 0.06f * (L[i] - y1); y2 += 0.06f * (y1 - y2); L[i] = R[i] = y2 * 3; } // 2-pole LP ~460 Hz
      run(e, L, R, 512);
      float mx = -99, mn = 99; for (int b = 0; b < 32; ++b) { mx = std::max(mx, e.getBandGainDb(b)); mn = std::min(mn, e.getBandGainDb(b)); }
      std::printf("    Recover on dark mix: band gains %.2f .. %.2f dB\n", mn, mx);
      CHECK(mx > 1.0f && mn >= -0.01f && mx <= 12.0f, "Recover range wrong (%.2f..%.2f)", mn, mx); }
    // Limits
    { WaveSurferEngine e; e.prepare(48000); e.setParameters(P(1, 1, 0, 1, 0, 1, 2000, 4000));
      std::vector<float> L, R; gen(Sig::PinkPlusTone, 48000, 48000 * 2, L, R); run(e, L, R, 512);
      float outside = 0; for (int b = 0; b < 32; ++b) { float f = e.getBandFreq(b); if (f < 1400 || f > 5800) outside = std::max(outside, std::abs(e.getBandGainDb(b))); }
      CHECK(outside < 0.05f, "processing outside limits (%.3f dB)", outside); }
    // crossed limits behave like swapped
    { WaveSurferEngine a, b; a.prepare(48000); b.prepare(48000); a.setParameters(P(1, 1, 0, 0, 0, 1, 4000, 2000)); b.setParameters(P(1, 1, 0, 0, 0, 1, 2000, 4000));
      std::vector<float> L, R, L2, R2; gen(Sig::Pink, 48000, 48000, L, R); L2 = L; R2 = R; run(a, L, R, 512); run(b, L2, R2, 512);
      CHECK(!std::memcmp(L.data(), L2.data(), L.size() * 4), "crossed limits not handled as swapped"); }
    // Pink noise: moderate, bounded curve
    { WaveSurferEngine e; e.prepare(48000); e.setParameters(P(1, 1, 0, 0, 0, 1));
      std::vector<float> L, R; gen(Sig::Pink, 48000, 48000 * 3, L, R); run(e, L, R, 512);
      float m = 0; for (int b = 0; b < 32; ++b) m = std::max(m, std::abs(e.getBandGainDb(b)));
      std::printf("    pink noise, Recover+Tame 100%%: max |band gain| %.2f dB\n", m);
      CHECK(m <= 12.0f, "pink curve out of range"); }
}

static void testSilenceAfterLoud() {
    std::puts("[T10] loud -> near-silence: no noise-floor pumping");
    WaveSurferEngine e; e.prepare(48000); e.setParameters(P(1, 1, 1, 1, 0, 1));
    std::vector<float> L, R; gen(Sig::Pink, 48000, 48000 * 4, L, R);
    for (size_t i = 48000; i < L.size(); ++i) { L[i] *= 3e-6f; R[i] *= 3e-6f; } // -110 dBFS
    auto L0 = L; run(e, L, R, 512);
    float m = 0; for (int b = 0; b < 32; ++b) m = std::max(m, std::abs(e.getBandGainDb(b)) - std::abs(3.0f * std::clamp((float)std::log2(e.getBandFreq(b) / 1000) / 4, -1.f, 1.f)));
    double lift = rmsDb(L, 48000 * 3) - rmsDb(L0, 48000 * 3);
    std::printf("    residual adaptive gain %.2f dB, level change %.2f dB\n", m, lift);
    CHECK(m < 0.5f, "gains stuck after signal dropped (%.2f dB)", m);
    CHECK(lift < 3.5, "noise floor lifted %.2f dB", lift);
}

static void testGainMatchDelta() {
    std::puts("[T11] gain match holds loudness; delta = wet - dry; mono == stereo-L");
    { WaveSurferEngine e; e.prepare(48000); Params p = P(1, 0, 1, 1, 0, 1); p.gainMatch = true; e.setParameters(p);
      std::vector<float> L, R; gen(Sig::Pink, 48000, 48000 * 6, L, R); auto L0 = L; run(e, L, R, 512);
      WaveSurferEngine u; u.prepare(48000); u.setParameters(P(1, 0, 1, 1, 0, 1)); std::vector<float> L2, R2; gen(Sig::Pink, 48000, 48000 * 6, L2, R2); run(u, L2, R2, 512);
      double dm = rmsDb(L, 48000 * 4) - rmsDb(L0, 48000 * 4), du = rmsDb(L2, 48000 * 4) - rmsDb(L0, 48000 * 4);
      std::printf("    level change: unmatched %+.2f dB, matched %+.2f dB (gm %.2f dB)\n", du, dm, e.getGainMatchDb());
      CHECK(std::abs(dm) < 1.0, "gain match off by %.2f dB", dm); }
    { WaveSurferEngine w, d; w.prepare(48000); d.prepare(48000); Params p = P(1, 1, 0, 0.5f, 0, 1); w.setParameters(p); p.delta = true; d.setParameters(p);
      std::vector<float> L, R; gen(Sig::Pink, 48000, 48000 * 2, L, R); auto dry = L; auto L2 = L, R2 = R;
      run(w, L, R, 512); run(d, L2, R2, 512); float err = 0;
      for (size_t i = 48000; i < L.size(); ++i) err = std::max(err, std::abs(L2[i] - (L[i] - dry[i])));
      CHECK(err < 1e-5f, "delta mismatch %g", err); }
    { WaveSurferEngine s, m; s.prepare(48000); m.prepare(48000); s.setParameters(P(1, 1, 0, 0.5f, 3, 1)); m.setParameters(P(1, 1, 0, 0.5f, 3, 1));
      std::vector<float> L, R; gen(Sig::Pink, 48000, 48000, L, R); R = L; auto M = L, dummy = L;
      run(s, L, R, 512, true); run(m, M, dummy, 512, false);
      CHECK(!std::memcmp(L.data(), M.data(), L.size() * 4), "mono path differs from stereo L=R"); }
}

static void testEdgeCalls() {
    std::puts("[T12] edge calls: 0 samples, null pointers, 0 channels, process before setParameters, re-prepare");
    WaveSurferEngine e; e.prepare(44100);
    std::vector<float> L(64, 0.1f), R(64, 0.1f); float* ch[2] = { L.data(), R.data() };
    e.process(ch, 2, 64);           // before setParameters
    e.process(ch, 2, 0); e.process(nullptr, 2, 64); e.process(ch, 0, 64); e.process(ch, 5, 64);
    e.prepare(192000); e.setParameters(P(1, 1, 1, 1, 12, 1)); e.process(ch, 2, 64);
    e.prepare(22050); e.process(ch, 2, 64);
    Params bad; bad.recover = std::numeric_limits<float>::quiet_NaN(); bad.boostDb = std::numeric_limits<float>::infinity(); bad.lowHz = -5;
    e.setParameters(bad); e.process(ch, 2, 64);
    CHECK(allFinite(L) && allFinite(R), "edge calls produced non-finite output");
}

static void testDenormal() {
    std::puts("[T13] denormal inputs without FTZ (no slowdown)");
    auto timeIt = [](Sig s) { WaveSurferEngine e; e.prepare(48000); e.setParameters(P(1, 1, 0, 0.5f, 0, 1));
        std::vector<float> L, R; gen(s, 48000, 48000 * 4, L, R);
        // warm up with pink so bells are active, then switch signal
        std::vector<float> wl, wr; gen(Sig::Pink, 48000, 48000, wl, wr); run(e, wl, wr, 512);
        auto t0 = Clock::now(); run(e, L, R, 512); return std::chrono::duration<double>(Clock::now() - t0).count(); };
    double tn = timeIt(Sig::Pink), td = timeIt(Sig::Denormal), ts = timeIt(Sig::Silence);
    std::printf("    pink %.1f ms, denormal %.1f ms, silence %.1f ms (4 s audio)\n", tn * 1e3, td * 1e3, ts * 1e3);
    CHECK(td < tn * 2.5, "denormal slowdown x%.1f", td / tn);
}

static void testLongRun() {
    std::puts("[T14] long run: 60 s @192k, max settings, +24 dB noise bursts");
    WaveSurferEngine e; e.prepare(192000); Params p = P(1, 1, -1, 1, 12, 1); p.gainMatch = true; e.setParameters(p);
    bool fin = true; std::vector<float> L, R; float worst = 0;
    for (int s = 0; s < 60; ++s) { gen(s % 7 == 3 ? Sig::Hot : Sig::Pink, 192000, 192000, L, R, s + 11); run(e, L, R, 0, true, nullptr, s);
        fin &= allFinite(L) && allFinite(R); worst = std::max(worst, peak(L)); }
    std::printf("    worst output peak %.1f\n", worst);
    CHECK(fin, "non-finite in long run");
}

static void benchmark() {
    std::puts("[CPU] single core, stereo, block 512, Recover 70% / Tame 50% (all bells active)");
    std::puts("      per-block time = fastest of 5 repeats (removes OS/VM scheduling noise)");
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        const int secs = 10, bs = 512; std::vector<float> L0, R0; gen(Sig::Pink, fs, (int)fs * secs, L0, R0);
        const int nb = (int)L0.size() / bs; std::vector<double> best(nb, 1e9);
        for (int rep = 0; rep < 5; ++rep) {
            WaveSurferEngine e; e.prepare(fs); e.setParameters(P(0.7f, 0.5f, 0, 0.3f, 1, 1));
            auto L = L0, R = R0;
            for (int k = 0; k < nb; ++k) { float* ch[2] = { L.data() + k * bs, R.data() + k * bs }; auto t0 = Clock::now(); e.process(ch, 2, bs);
                best[k] = std::min(best[k], std::chrono::duration<double>(Clock::now() - t0).count()); }
        }
        double total = 0, worst = 0; for (int k = 10; k < nb; ++k) { total += best[k]; worst = std::max(worst, best[k]); }
        double avg = total / (nb - 10), blockDur = bs / fs;
        std::printf("    %6.0f Hz: avg %.3f%% of one core, worst block %.3f%% (x%.2f of avg)\n", fs, 100 * avg / blockDur, 100 * worst / blockDur, worst / avg);
    }
}

int main() {
    std::puts("=== WaveSurfer extreme-level quality check ===");
    testClipper(); testKalman(); testIso(); testNull(); testExtremeGrid(); testNaN(); testRatesBlocks();
    testAutomation(); testBehaviour(); testSilenceAfterLoud(); testGainMatchDelta(); testEdgeCalls(); testDenormal(); testLongRun();
    benchmark();
    std::printf("\n=== %d checks passed, %d failed ===\n", g_pass, g_fail);
    for (auto& f : g_failures) std::printf("  - %s\n", f.c_str());
    return g_fail ? 1 : 0;
}
