# WaveSurfer — project guide

Perceptual spectral unmasking EQ (Gullfoss-style). JUCE 9, C++17, CMake.
Brand: Quantum Research Audio. Free/research release.

## Architecture (keep it this way)
- `Source/dsp/WaveSurferDSP.h` is JUCE-free and header-only, so it can be stress-tested without JUCE.
- Audio rate: 32 TPT band-passes on Mid (analysis only, never summed) + 16 TPT bells per channel.
- Control rate (~1.5 kHz, N = fs/1500 samples): Kalman (dB), ISO 226 weighting, two-pass
  spreading (O(N)), gain law, bell coefficients, gain match.
- Audio is processed in chunks aligned to control blocks, so output is bit-identical at any host block size.
- Zero latency; dry is aligned for delta and bypass.

## Quality protocol (run before every release)
1. `WaveSurferStressTest`: all 256 parameter corners x 6 signals, NaN/inf/1e30 injection,
   22.05k–384k x block sizes 1–8192, automation torture, nulls, denormals, 60 s long run, CPU.
2. `WaveSurferHostTest` on the built VST3: random extreme automation, state round trip, mono, editor lifecycle.
3. `WaveSurferSnapshot --editor-stress`: real windows opened/closed under audio + automation load.
4. Sanitizer build of the stress test: `g++ -std=c++17 -O1 -g -fsanitize=address,undefined -ISource Tests/StressTest.cpp`.
5. Zero warnings under `juce_recommended_warning_flags`.

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
