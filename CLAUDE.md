# WaveSurfer — project guide

Perceptual spectral unmasking EQ (Gullfoss-style). JUCE 8.0.10 (same as Flux 512), C++17, CMake.
Targets: macOS 10.13+ universal (VST3/AU/Standalone) and Windows x64 (VST3/Standalone). The QRA rules in the
Flux 512 repo's CLAUDE.md apply here too.
Brand: Quantum Research Audio. Free/research release.

## Architecture (keep it this way)
- `Source/dsp/WaveSurferDSP.h` is JUCE-free and header-only, so it can be stress-tested without JUCE.
- Audio rate: 32 TPT band-passes on Mid (analysis only, never summed) + 16 TPT bells per channel.
- Control rate (~1.5 kHz, N = fs/1500 samples): Kalman (dB), ISO 226 weighting, two-pass
  spreading (O(N)), gain law, bell coefficients, gain match.
- Audio is processed in chunks aligned to control blocks, so output is bit-identical at any host block size.
- Two references: Ear (ISO 226 + Bark masking, fast) and Pink (long-term balance vs pink, slow, power-averaged).
- Zero latency by default; optional 20 ms lookahead (delay line always runs; latency reported via AsyncUpdater).
  Dry is taken after the delay, so delta and bypass stay aligned.
- Loudness match is predictive: K-weighted band power x exact bell response at band centres. No feedback loop.

## Quality protocol (run before every release)
1. `WaveSurferStressTest` (73 checks): all 256 parameter corners x 6 signals x modes, NaN/inf/1e30 injection,
   22.05k–384k x block sizes 1–8192, automation torture, nulls, denormals, 60 s long run, CPU.
2. `WaveSurferHostTest` on the built VST3: random extreme automation, state round trip, mono, editor lifecycle,
   lookahead latency reported to the host.
3. `WaveSurferSnapshot --editor-stress`: real windows opened/closed under audio + automation load.
4. Sanitizer build of the stress test: `g++ -std=c++17 -O1 -g -fsanitize=address,undefined -ISource Tests/StressTest.cpp`.
5. Zero warnings under `juce_recommended_warning_flags`.
6. CI also runs REAPER 7.83 on macOS 15: loads VST3 + AU, extreme parameters, opens windows, renders.
8. `WaveSurferPluginStress`: the QRA generic extreme-parameter test from Flux 512 (548 cases), on macOS and Windows.
7. GitHub has no macOS 10.13 runner: check imports against the 10.13 SDK exports, and test on the real High Sierra Mac.

## Lessons learned (apply to future plugins)
1. A scalar Kalman with fixed Q/R is just a one-pole. Set Q from a time constant: K = 1 − e^(−1/(τ·rate)), Q = K²R/(1−K).
   Scaling Q by 1/fs is wrong (K ∝ √Q), so τ drifts by √(rate ratio).
2. Envelopes must be slower than the band's period (τ ≥ ~4/fc). Otherwise the gains follow the waveform and distort.
3. Clamp garbage input in the engine (here ±1000 = +60 dBFS). A single 1e30 sample made resonant filters ring for seconds.
4. Flush denormal inputs inside the engine (DAZ). Without FTZ, denormal input cost 39x CPU; don't rely only on the host's ScopedNoDenormals.
5. For a bit-exact null at Amount 0, keep DC blockers and clippers out of the audio path, snap smoothed gains to exact 0,
   and skip filters whose gain is exactly 0 dB.
6. juce::SmoothedValue::reset() doesn't set the current value. Snap current = target on the first block after prepare.
7. Shared or cloud machines produce fake CPU spikes. Measure per-block time as the minimum over several repeats.
8. Narrow resonances get diluted by band smoothing and bell pairing. For cuts, weight toward the deeper band of the pair,
   and de-weight the leaky ±1 neighbours when measuring prominence.
9. JUCE CMake doesn't generate JuceHeader.h unless asked. Include modules directly (`<juce_audio_utils/juce_audio_utils.h>`).
10. Headless Linux JUCE builds need JUCE_USE_XRANDR/XINERAMA/XRENDER/XCURSOR/XINPUT=0 and JUCE_ALSA/JACK=0.
11. VST3 hosts deliver parameter changes on the next process() call. Tests must process a block before reading state.
12. Bus layouts can only change while the plugin is inactive (releaseResources first).
13. Use the exact digital bell response for the GUI curve: w = tan(πf/fs)/tan(πfc/fs), H = (1−w²+jkA²w)/(1−w²+jkw).
    Verified against the simulated filter to 0.004 dB.
14. Xcode 27 drops deployment targets below macOS 12. Keep an older Xcode for High Sierra builds.
15. pkgbuild --analyze doesn't always write BundleIsRelocatable. Delete then Add it (bool false), or the installer
    may "relocate" a plugin into some other copy on the user's disk.
16. In packaging scripts, make output paths absolute before any `cd`. A relative zip path silently went to a temp dir.
17. Let CI publish releases (GITHUB_TOKEN inside Actions); keep the version in CMakeLists.txt as the single source.
18. Never average noisy band levels in dB: E[log] < log E, so low bands (few cycles per window) read 3-5 dB low.
    Smooth in power first, then convert. Calibrate each band's pink-noise reading analytically.
19. "x + 1*(dry - x)" is not exactly dry in float. Select the dry/wet endpoint directly when a crossfade sits at 0 or 1.
20. Predicting loudness from the *target* curve overshot by 1.3-1.7 dB. Use the filters' exact response at the band centres.
21. A linear tilt cancels out of any "band vs neighbours" measure. To make a tilt control audible, tilt the decision
    thresholds per frequency as well as the input.
22. Name the new behaviour, not the old knob: when a parameter changes meaning (Boost), add a new one (Output) for the old job.
23. "Passes on macOS 15" says nothing about High Sierra. Check the x86_64 slice's imports against the 10.13 SDK .tbd
    exports, keep it free of AVX/SSE4.2 (Apple's default x86_64 CPU is Penryn: SSE4.1), and test on the real machine.
24. REAPER on a fresh CI machine stops at "You have not selected an audio device" - click No via osascript.
25. Use the JUCE release that is proven on the oldest target machine (8.0.10, as Flux 512), not JUCE master.
    WaveSurfer 1.0/1.1 on JUCE 9 master passed every CI check but crashed in REAPER on the user's macOS 10.13 Mac.
26. JUCE 8 vs 9 API: AudioPluginFormatManager::addFormat takes a raw pointer in 8; createEditorAndMakeActive is 9-only
    (use createEditorIfNeeded). JUCE 8's juceaide needs X11 extension headers on Linux even with JUCE_USE_X*=0:
    use stub headers via CPATH.
