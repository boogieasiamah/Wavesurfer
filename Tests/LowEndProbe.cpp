// LowEndProbe: what does WaveSurfer do to the spectrum, especially below 300 Hz?
// Feeds steady, music-shaped noise and reports the applied curve (exact bell response,
// averaged over the last 6 s of a 16 s run) in octave-ish regions.
#include "dsp/WaveSurferDSP.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include <cctype>
#include <cstdint>
#include <algorithm>

struct Rng { uint64_t s = 0x9E3779B97F4A7C15ull; float next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (float)((s >> 11) * (1.0 / 9007199254740992.0)) * 2.0f - 1.0f; } };
struct Pink { float b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0; Rng r;
    float next() { float w = r.next(); b0=0.99886f*b0+w*0.0555179f; b1=0.99332f*b1+w*0.0750759f; b2=0.96900f*b2+w*0.1538520f;
        b3=0.86650f*b3+w*0.3104856f; b4=0.55000f*b4+w*0.5329522f; b5=-0.7616f*b5-w*0.0168980f; float p=b0+b1+b2+b3+b4+b5+b6+w*0.5362f; b6=w*0.115926f; return p*0.11f; } };

struct Biquad { double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    float run(float x) { double y = b0*x + z1; z1 = b1*x - a1*y + z2; z2 = b2*x - a2*y; return (float) y; }
    static Biquad rbj(int type, double fs, double f0, double q, double gdb) {   // 0 peak, 1 lowshelf, 2 highshelf
        const double A = std::pow(10.0, gdb / 40.0), w = 2 * 3.14159265358979 * f0 / fs, cw = std::cos(w), al = std::sin(w) / (2 * q);
        double b0, b1, b2, a0, a1, a2;
        if (type == 0) { b0 = 1 + al*A; b1 = -2*cw; b2 = 1 - al*A; a0 = 1 + al/A; a1 = -2*cw; a2 = 1 - al/A; }
        else { const double s = 2*std::sqrt(A)*al, sg = type == 1 ? 1.0 : -1.0;
            b0 = A*((A+1) - sg*(A-1)*cw + s); b1 = sg*2*A*((A-1) - sg*(A+1)*cw); b2 = A*((A+1) - sg*(A-1)*cw - s);
            a0 = (A+1) + sg*(A-1)*cw + s; a1 = -sg*2*((A-1) + sg*(A+1)*cw); a2 = (A+1) + sg*(A-1)*cw - s; }
        Biquad q2; q2.b0=b0/a0; q2.b1=b1/a0; q2.b2=b2/a0; q2.a1=a1/a0; q2.a2=a2/a0; return q2; } };

struct Shape { const char* name; std::vector<Biquad> f; };

static std::vector<Shape> shapes(double fs) {
    std::vector<Shape> s;
    s.push_back({"pink (flat per octave)", {}});
    // Average commercial-mix shape [estimate]: pink with a bass shelf and a top roll-off
    s.push_back({"mix-shaped (+4 dB <120 Hz, -6 dB >5 kHz)", {Biquad::rbj(1, fs, 120, 0.7, 4), Biquad::rbj(2, fs, 5000, 0.7, -6)}});
    s.push_back({"muddy mix (mix + 6 dB @ 250 Hz)", {Biquad::rbj(1, fs, 120, 0.7, 4), Biquad::rbj(2, fs, 5000, 0.7, -6), Biquad::rbj(0, fs, 250, 1.0, 6)}});
    s.push_back({"bass-heavy (+10 dB <90 Hz)", {Biquad::rbj(1, fs, 90, 0.7, 10), Biquad::rbj(2, fs, 5000, 0.7, -6)}});
    s.push_back({"thin mix (-10 dB <150 Hz)", {Biquad::rbj(1, fs, 150, 0.7, -10), Biquad::rbj(2, fs, 5000, 0.7, -6)}});
    return s;
}

// --- synthetic "song": kick, bass line, chords, vocal-like formant tone, hats, 120 bpm ---
struct Song {
    double fs, t = 0; Rng r; Biquad hatHp, vowelA, vowelB; double bassPh = 0, kickPh = 0, ph[6] = {0}, vPh = 0;
    explicit Song(double f) : fs(f) { hatHp = Biquad::rbj(2, fs, 7000, 0.7, 0); hatHp = Biquad::rbj(2, fs, 6000, 0.7, 12);
        vowelA = Biquad::rbj(0, fs, 700, 4, 14); vowelB = Biquad::rbj(0, fs, 1200, 4, 12); }
    void next(float& l, float& rr) {
        const double tw = 2 * 3.14159265358979, beat = 0.5, tb = std::fmod(t, beat), bar = std::fmod(t, 4 * beat);
        const int step = (int) (t / beat);
        // kick: 55 Hz pitch-drop, 250 ms decay
        const double kf = 50 + 90 * std::exp(-tb / 0.02); kickPh += tw * kf / fs;
        const double kick = 0.9 * std::sin(kickPh) * std::exp(-tb / 0.18);
        // bass: notes cycling E1 A1 D2 G1 with 3 harmonics
        static const double notes[4] = {41.2, 55.0, 73.4, 49.0}; const double bf = notes[(step / 2) % 4]; bassPh += tw * bf / fs;
        const double bass = 0.35 * (std::sin(bassPh) + 0.5 * std::sin(2 * bassPh) + 0.25 * std::sin(3 * bassPh)) * (0.6 + 0.4 * std::exp(-tb / 0.3));
        // chords: 3 notes, 6 harmonics each, around 200-800 Hz
        static const double ch[2][3] = {{220.0, 277.2, 329.6}, {196.0, 246.9, 293.7}}; double chord = 0;
        for (int n = 0; n < 3; ++n) { const double f0 = ch[(step / 8) % 2][n];
            for (int h = 1; h <= 6; ++h) chord += std::sin(tw * f0 * h * t + n) / (h * h * 0.7 + 0.3); }
        chord *= 0.05;
        // vocal-like: sawtooth 180 Hz through two formants, phrase on/off
        vPh += 180.0 / fs; if (vPh > 1) vPh -= 1; double v = (2 * vPh - 1) * ((bar < 1.5) ? 1.0 : 0.0);
        v = 0.06 * vowelB.run((float) vowelA.run((float) v));
        // hats: noise bursts every 1/2 beat
        const double th = std::fmod(t, beat / 2); const double hat = 0.12 * hatHp.run(r.next()) * std::exp(-th / 0.03);
        const double snare = (step % 2 == 1) ? 0.25 * r.next() * std::exp(-tb / 0.08) : 0.0;
        l = (float) (kick + bass + 0.9 * chord + v + 0.8 * hat + snare);
        rr = (float) (kick + bass + 1.1 * chord + v + 1.2 * hat + snare);
        t += 1.0 / fs;
    }
};

struct BandMeter { Biquad a, b; double e = 0;
    BandMeter(double fs, double lo, double hi) { const double fc = std::sqrt(lo * hi), q = fc / (hi - lo);
        auto bp = [&]() { const double w = 2 * 3.14159265358979 * fc / fs, al = std::sin(w) / (2 * q), a0 = 1 + al;
            Biquad x; x.b0 = al / a0; x.b1 = 0; x.b2 = -al / a0; x.a1 = -2 * std::cos(w) / a0; x.a2 = (1 - al) / a0; return x; };
        a = bp(); b = bp(); }
    void add(float x) { const float y = b.run(a.run(x)); e += (double) y * y; } };

struct KMeter { double x1=0,x2=0,y1=0,y2=0,w1=0,w2=0,z1=0,z2=0,e=0;   // BS.1770 @ 48 kHz
    void add(float x) { const double b0 = 1.53512485958697, b1 = -2.69169618940638, b2 = 1.19839281085285, a1 = -1.69065929318241, a2 = 0.73248077421585;
        const double c1 = -1.99004745483398, c2 = 0.99007225036621;
        double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y;
        double z = y - 2 * w1 + w2 - c1 * z1 - c2 * z2; w2 = w1; w1 = y; z2 = z1; z1 = z; e += z * z; } };

static void songTest(double fs, float levelDb) {
    const float regions[][2] = {{20,60},{60,150},{150,400},{400,1500},{1500,5000},{5000,16000}};
    struct Set { const char* name; float rec, tame; wavesurfer::Reference ref; };
    const Set sets[] = {{"Ear  Rec 40% Tame 40%", 0.4f, 0.4f, wavesurfer::Reference::Ear}, {"Ear  Rec 100%", 1, 0, wavesurfer::Reference::Ear},
                        {"Ear  Tame 100%", 0, 1, wavesurfer::Reference::Ear}, {"Ear  both 100%", 1, 1, wavesurfer::Reference::Ear},
                        {"Pink both 100%", 1, 1, wavesurfer::Reference::Pink}};
    std::printf("\nsynthetic song at %.0f dBFS RMS: real output/input energy per region (dB), last 8 s of 20 s\n", levelDb);
    std::printf("%-24s  20-60  60-150 150-400 0.4-1.5k 1.5-5k  5-16k  | curve swing 60-150 (p-p dB)\n", "setting");
    for (auto& st : sets) {
        wavesurfer::WaveSurferEngine e; e.prepare(fs, 512);
        wavesurfer::Params p; p.recover = st.rec; p.tame = st.tame; p.reference = st.ref; p.gainMatch = true; e.setParameters(p);
        Song s(fs); double acc = 0; { Song t2(fs); for (int i = 0; i < (int) (4 * fs); ++i) { float a, b; t2.next(a, b); acc += 0.5 * (a * a + b * b); } }
        const float g = std::pow(10.0f, levelDb / 20.0f) / (float) std::sqrt(acc / (4 * fs));
        std::vector<BandMeter> mi, mo; for (auto& r : regions) { mi.emplace_back(fs, r[0], r[1]); mo.emplace_back(fs, r[0], r[1]); }
        std::vector<float> L(512), R(512), IL(512), IR(512); float* chp[2] = {L.data(), R.data()};
        const int blocks = (int) (20 * fs / 512), from = blocks - (int) (8 * fs / 512);
        float cMin = 1e9f, cMax = -1e9f; KMeter kiL, kiR, koL, koR;
        for (int k = 0; k < blocks; ++k) {
            for (int i = 0; i < 512; ++i) { float a, b; s.next(a, b); L[i] = IL[i] = g * a; R[i] = IR[i] = g * b; }
            e.process(chp, 2, 512);
            if (k >= from) {
                for (int i = 0; i < 512; ++i) { kiL.add(IL[i]); kiR.add(IR[i]); koL.add(L[i]); koR.add(R[i]); }
                for (size_t r = 0; r < mi.size(); ++r) for (int i = 0; i < 512; ++i) { mi[r].add(IL[i] + IR[i]); mo[r].add(L[i] + R[i]); }
                const float c = e.responseDb(90.0); cMin = std::min(cMin, c); cMax = std::max(cMax, c);
            }
        }
        std::printf("%-24s", st.name);
        for (size_t r = 0; r < mi.size(); ++r) std::printf(" %+6.1f", 10 * std::log10(mo[r].e / mi[r].e));
        std::printf("  | %.1f   K-loudness %+.2f dB\n", cMax - cMin, 10 * std::log10((koL.e + koR.e) / (kiL.e + kiR.e)));
    }
}

// vocal-like source over quiet low-mid room/bleed (what Recover might turn into mud)
static void roomTest(double fs, float roomRelDb) {
    const float regions[][2] = {{20,60},{60,150},{150,400},{400,1500},{1500,5000},{5000,16000}};
    std::printf("\nvocal + room boom %.0f dB below it (low-passed pink <300 Hz): output/input energy per region (dB)\n", roomRelDb);
    std::printf("%-24s  20-60  60-150 150-400 0.4-1.5k 1.5-5k  5-16k  | max curve 60-400 Hz\n", "setting");
    const float recs[] = {0.4f, 1.0f, 2.0f};
    for (float rec : recs) for (int tm = 0; tm < 2; ++tm) {
        wavesurfer::WaveSurferEngine e; e.prepare(fs, 512);
        wavesurfer::Params p; p.recover = rec; p.tame = tm ? rec : 0.0f; p.gainMatch = true; e.setParameters(p);
        Rng nr; Pink pk; Biquad lp1 = Biquad::rbj(0, fs, 1, 1, 0), fA = Biquad::rbj(0, fs, 700, 4, 14), fB = Biquad::rbj(0, fs, 1200, 4, 12), fC = Biquad::rbj(0, fs, 2800, 4, 10);
        // 4th-order low-pass at 300 Hz (two RBJ LPs)
        auto lp = [&](double f0) { const double w = 2*3.14159265358979*f0/fs, al = std::sin(w)/(2*0.707), a0 = 1+al;
            Biquad x; x.b0 = (1-std::cos(w))/2/a0; x.b1 = (1-std::cos(w))/a0; x.b2 = x.b0; x.a1 = -2*std::cos(w)/a0; x.a2 = (1-al)/a0; return x; };
        Biquad l1 = lp(300), l2 = lp(300); (void) lp1;
        std::vector<BandMeter> mi, mo; for (auto& r : regions) { mi.emplace_back(fs, r[0], r[1]); mo.emplace_back(fs, r[0], r[1]); }
        std::vector<float> L(512), R(512), I(512); float* chp[2] = {L.data(), R.data()};
        const float vg = 0.1f, rg = vg * std::pow(10.0f, roomRelDb / 20.0f) * 6.0f; double ph = 0; float cMax = -99;
        const int blocks = (int) (20 * fs / 512), from = blocks - (int) (8 * fs / 512);
        for (int k = 0; k < blocks; ++k) {
            for (int i = 0; i < 512; ++i) {
                const double f0 = 180 + 20 * std::sin(2 * 3.14159265358979 * 0.3 * (k * 512 + i) / fs); ph += f0 / fs; if (ph > 1) ph -= 1;
                const float v = vg * fC.run(fB.run(fA.run((float) (2 * ph - 1))));
                const float room = rg * l2.run(l1.run(pk.next()));
                L[i] = R[i] = I[i] = v + room; }
            e.process(chp, 2, 512);
            if (k >= from) { for (size_t r = 0; r < mi.size(); ++r) for (int i = 0; i < 512; ++i) { mi[r].add(I[i]); mo[r].add(L[i]); }
                for (double f = 60; f <= 400; f *= 1.1) cMax = std::max(cMax, e.responseDb(f)); }
        }
        char nm[64]; std::snprintf(nm, sizeof nm, "Ear Rec %3.0f%% Tame %3.0f%%", rec * 100, tm ? rec * 100 : 0.0f);
        std::printf("%-24s", nm);
        for (size_t r = 0; r < mi.size(); ++r) std::printf(" %+6.1f", 10 * std::log10(mo[r].e / mi[r].e));
        std::printf("  | %+.1f dB\n", cMax);
    }
}

// --- test pack: write the probe signals as 48 kHz 32-bit float stereo WAVs ---
static void writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int fs) {
    FILE* f = std::fopen(path.c_str(), "wb"); if (! f) return;
    const uint32_t n = (uint32_t) L.size(), data = n * 6, chunk = 36 + data; const uint16_t fmt = 1, ch = 2, bits = 24, align = 6;
    const uint32_t sr = (uint32_t) fs, br = sr * 6, sixteen = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&chunk, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f); std::fwrite(&sixteen, 4, 1, f);
    std::fwrite(&fmt, 2, 1, f); std::fwrite(&ch, 2, 1, f); std::fwrite(&sr, 4, 1, f); std::fwrite(&br, 4, 1, f);
    std::fwrite(&align, 2, 1, f); std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
    auto put = [&](float x) { const int32_t v = (int32_t) std::lround(std::clamp(x, -1.0f, 1.0f - 1.0f / 8388608.0f) * 8388608.0f);
        const unsigned char b[3] = {(unsigned char) (v & 255), (unsigned char) ((v >> 8) & 255), (unsigned char) ((v >> 16) & 255)}; std::fwrite(b, 1, 3, f); };
    for (uint32_t i = 0; i < n; ++i) { put(L[i]); put(R[i]); }
    std::fclose(f);
}

static void makePack(const std::string& dir) {
    const double fs = 48000.0; const int n = (int) (15 * fs);
    auto norm = [&](std::vector<float>& L, std::vector<float>& R, float rmsDb) {
        double a = 0; for (int i = 0; i < n; ++i) a += 0.5 * (L[i] * L[i] + R[i] * R[i]);
        const float g = std::pow(10.0f, rmsDb / 20.0f) / (float) std::sqrt(a / n); for (int i = 0; i < n; ++i) { L[i] *= g; R[i] *= g; } };
    int idx = 1;
    for (auto& sh : shapes(fs)) {
        std::vector<float> L(n), R(n); auto fl = sh.f, fr = sh.f; Pink pl, pr; pr.r.s ^= 0x1234567ull;
        for (int i = 0; i < n; ++i) { float a = pl.next(), b = pr.next(); for (auto& q : fl) a = q.run(a); for (auto& q : fr) b = q.run(b);
            L[i] = 0.8f * a + 0.2f * b; R[i] = 0.2f * a + 0.8f * b; }
        norm(L, R, -18); static const char* files[] = {"pink", "mix_shaped", "muddy_mix_250Hz", "bass_heavy", "thin_mix"};
        char fn[256]; std::snprintf(fn, sizeof fn, "%s/%02d_%s.wav", dir.c_str(), idx, files[idx - 1]); ++idx; writeWav(fn, L, R, (int) fs);
    }
    { std::vector<float> L(n), R(n); Song s(fs); for (int i = 0; i < n; ++i) s.next(L[i], R[i]); norm(L, R, -14);
      writeWav(dir + "/06_synthetic_song.wav", L, R, (int) fs); }
    { std::vector<float> L(n), R(n); const double tw = 2 * 3.14159265358979;   // log sweep 20 Hz-20 kHz at -30 dBFS over pink at -18
      Pink pk; double ph = 0; for (int i = 0; i < n; ++i) { const double t = (double) i / fs, f = 20 * std::pow(1000.0, t / 15.0); ph += tw * f / fs;
        const float x = 0.0316f * (float) std::sin(ph) + 0.6f * pk.next(); L[i] = R[i] = x; }
      writeWav(dir + "/07_sweep_over_pink.wav", L, R, (int) fs); }
    std::printf("wrote test pack to %s\n", dir.c_str());
}

// --- offline render of a pack file (24-bit PCM stereo, 44-byte header as written above) ---
static bool readPack(const std::string& path, std::vector<float>& L, std::vector<float>& R) {
    FILE* f = std::fopen(path.c_str(), "rb"); if (! f) return false;
    unsigned char h[44]; if (std::fread(h, 1, 44, f) != 44) { std::fclose(f); return false; }
    const uint32_t data = h[40] | (h[41] << 8) | (h[42] << 16) | ((uint32_t) h[43] << 24); const uint32_t n = data / 6;
    L.resize(n); R.resize(n); unsigned char b[6];
    for (uint32_t i = 0; i < n && std::fread(b, 1, 6, f) == 6; ++i) {
        auto get = [&](int o) { int32_t v = b[o] | (b[o + 1] << 8) | (b[o + 2] << 16); if (v & 0x800000) v -= 0x1000000; return (float) v / 8388608.0f; };
        L[i] = get(0); R[i] = get(3); }
    std::fclose(f); return true;
}

static int render(int argc, char** argv) {   // --render in.wav out.wav recover tame [pink]
    std::vector<float> L, R; if (argc < 6 || ! readPack(argv[2], L, R)) { std::puts("usage: --render in out rec tame [pink]"); return 1; }
    wavesurfer::WaveSurferEngine e; e.prepare(48000.0, 512);
    wavesurfer::Params p; p.recover = (float) std::atof(argv[4]); p.tame = (float) std::atof(argv[5]);
    p.reference = (argc > 6 && std::string(argv[6]) == "pink") ? wavesurfer::Reference::Pink : wavesurfer::Reference::Ear; e.setParameters(p);
    for (size_t i = 0; i < L.size(); i += 512) { const int n = (int) std::min<size_t>(512, L.size() - i); float* ch[2] = {L.data() + i, R.data() + i}; e.process(ch, 2, n); }
    writeWav(argv[3], L, R, 48000); return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--render") return render(argc, argv);
    if (argc > 1 && std::string(argv[1]) == "--pack") { makePack(argc > 2 ? argv[2] : "."); return 0; }
    if (argc > 3) { roomTest(48000.0, (float) std::atof(argv[3])); return 0; }
    if (argc > 2) { songTest(48000.0, (float) std::atof(argv[2])); return 0; }
    const double fs = 48000.0;
    const float levelDb = argc > 1 ? (float) std::atof(argv[1]) : -18.0f;   // RMS of the test signal
    const float regions[][2] = {{20,60},{60,150},{150,400},{400,1500},{1500,5000},{5000,16000}};
    struct Set { const char* name; float rec, tame; wavesurfer::Reference ref; };
    const Set sets[] = {{"Ear  Rec 100%", 1, 0, wavesurfer::Reference::Ear}, {"Ear  Tame 100%", 0, 1, wavesurfer::Reference::Ear},
                        {"Ear  both 100%", 1, 1, wavesurfer::Reference::Ear}, {"Pink both 100%", 1, 1, wavesurfer::Reference::Pink}};
    std::printf("test level %.0f dBFS RMS; applied curve in dB, mean over each region\n", levelDb);
    std::printf("%-44s %-15s  20-60  60-150 150-400 0.4-1.5k 1.5-5k  5-16k\n", "signal", "setting");
    for (auto& sh : shapes(fs)) {
        for (auto& st : sets) {
            wavesurfer::WaveSurferEngine e; e.prepare(fs, 512);
            wavesurfer::Params p; p.recover = st.rec; p.tame = st.tame; p.reference = st.ref; p.gainMatch = true;
            e.setParameters(p);
            auto fl = sh.f, fr = sh.f; Pink pl, pr; pr.r.s ^= 0x1234567ull;
            // calibrate RMS on a 2 s pre-roll
            double acc = 0; { auto f2 = sh.f; Pink t; for (int i = 0; i < 96000; ++i) { float x = t.next(); for (auto& b : f2) x = b.run(x); acc += x * x; } }
            const float g = std::pow(10.0f, levelDb / 20.0f) / (float) std::sqrt(acc / 96000.0);
            std::vector<float> L(512), R(512); float* ch[2] = {L.data(), R.data()};
            double sum[6] = {0}; int nAvg = 0;
            const int blocks = (int) (16 * fs / 512);
            for (int k = 0; k < blocks; ++k) {
                for (int i = 0; i < 512; ++i) { float a = pl.next(), b = pr.next(); for (auto& q : fl) a = q.run(a); for (auto& q : fr) b = q.run(b);
                    L[i] = g * (0.8f * a + 0.2f * b); R[i] = g * (0.2f * a + 0.8f * b); }
                e.process(ch, 2, 512);
                if (k > blocks - (int) (6 * fs / 512) && k % 24 == 0) {
                    for (int r = 0; r < 6; ++r) { double s = 0; int n = 0;
                        for (double f = regions[r][0]; f <= regions[r][1]; f *= 1.02) { s += e.responseDb(f); ++n; }
                        sum[r] += s / n; }
                    ++nAvg;
                }
            }
            std::printf("%-44s %-15s", sh.name, st.name);
            for (double v : sum) std::printf(" %+6.1f", v / nAvg);
            std::printf("\n");
        }
    }
}
