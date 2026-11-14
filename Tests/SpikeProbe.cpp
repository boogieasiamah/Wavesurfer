// SpikeProbe: per-block CPU of the engine, the number a DAW's spike meter shows.
// Each block's time is the minimum over REPEATS identical runs (VM noise only adds time),
// reported as % of that block's real-time budget: median, p99, worst, and worst/median.
#include "dsp/WaveSurferDSP.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>

struct Rng { uint64_t s = 0x9E3779B97F4A7C15ull; float next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (float)((s >> 11) * (1.0 / 9007199254740992.0)) * 2.0f - 1.0f; } };

struct Case { const char* name; float rec, tame; bool pink, look, delta; float boost; bool automate; };

static void run(double fs, int block, bool randomBlocks, const Case& c, bool silenceGaps) {
    const int REPEATS = 5, total = (int) (6 * fs);
    std::vector<int> sizes; { Rng r; int done = 0; while (done < total) { int n = randomBlocks ? 1 + (int) ((r.next() * 0.5f + 0.5f) * (block * 2 - 1)) : block; sizes.push_back(n); done += n; } }
    std::vector<double> best(sizes.size(), 1e30);
    std::vector<float> L(8192), R(8192);
    for (int rep = 0; rep < REPEATS; ++rep) {
        wavesurfer::WaveSurferEngine e; e.prepare(fs, block * 2);
        wavesurfer::Params p; p.recover = c.rec; p.tame = c.tame; p.boostDb = c.boost; p.lookahead = c.look; p.delta = c.delta;
        p.reference = c.pink ? wavesurfer::Reference::Pink : wavesurfer::Reference::Ear; e.setParameters(p);
        Rng sig; double t = 0; size_t pos = 0;
        for (size_t k = 0; k < sizes.size(); ++k) {
            const int n = sizes[k];
            const bool quiet = silenceGaps && std::fmod(t, 2.0) > 1.5;          // 0.5 s of silence every 2 s
            for (int i = 0; i < n; ++i) { const float x = quiet ? 0.0f : 0.2f * sig.next() + 0.3f * (float) std::sin(2 * 3.14159265 * 80 * (t + i / fs));
                L[i] = x; R[i] = 0.9f * x; }
            if (c.automate && k % 7 == 0) { p.recover = 2.0f * (0.5f + 0.5f * sig.next()); p.tame = 2.0f * (0.5f + 0.5f * sig.next()); p.boostDb = 50 * sig.next();
                p.lookahead = sig.next() > 0.6f; p.reference = sig.next() > 0 ? wavesurfer::Reference::Pink : wavesurfer::Reference::Ear; e.setParameters(p); }
            float* ch[2] = {L.data(), R.data()};
            const auto t0 = std::chrono::steady_clock::now();
            e.process(ch, 2, n);
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            best[k] = std::min(best[k], us);
            t += n / fs; pos += (size_t) n;
        }
    }
    std::vector<double> pct(sizes.size());
    for (size_t k = 0; k < sizes.size(); ++k) pct[k] = 100.0 * best[k] / (1e6 * sizes[k] / fs);
    std::vector<double> s = pct; std::sort(s.begin(), s.end());
    const double med = s[s.size() / 2], p99 = s[(size_t) (s.size() * 0.99)], mx = s.back();
    const size_t worstK = (size_t) (std::max_element(pct.begin(), pct.end()) - pct.begin());
    std::printf("%6.0f Hz  %-9s %-34s median %5.2f%%  p99 %5.2f%%  worst %6.2f%% (x%4.1f) at block %zu (n=%d)\n", fs,
        randomBlocks ? "random" : (std::to_string(block)).c_str(), c.name, med, p99, mx, mx / med, worstK, sizes[worstK]);
}

int main() {
    const Case cases[] = {
        {"default (Rec 0 / Tame 0)",           0, 0, false, false, false, 0, false},
        {"Ear Rec 100 / Tame 100",             1, 1, false, false, false, 0, false},
        {"Ear 200/200 + Boost +50 + lookahead", 2, 2, false, true, false, 50, false},
        {"Pink 200/200 + lookahead",           2, 2, true, true, false, 0, false},
        {"automation every 7 blocks",          1, 1, false, false, false, 0, true},
    };
    for (int b : {32, 64, 256, 1024}) for (auto& c : cases) run(48000, b, false, c, false);
    for (auto& c : cases) run(48000, 512, true, c, false);
    std::puts("-- with 0.5 s silence gaps (gate on/off, bells waking up)");
    for (auto& c : cases) run(48000, 64, false, c, true);
    std::puts("-- high rates");
    run(96000, 64, false, cases[2], false); run(192000, 64, false, cases[2], false);
}
