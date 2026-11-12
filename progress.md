# WaveSurfer — progress

## v1.1.1 (2026-11-11): rebuilt on the Flux 512 setup — macOS 10.13+ and Windows
- JUCE 9 master -> **JUCE 8.0.10** (FetchContent), the release Flux 512 runs on the same High Sierra Mac.
- New Windows build: VST3 + Standalone x64, static MSVC runtime, Inno Setup installer.
- Added the QRA generic extreme-parameter test from Flux 512 (548/548 pass locally; heaviest case 4% of real time).
- CI now also checks every required import of the Intel binary against the macOS 10.13 SDK.
- Local (Linux, JUCE 8): DSP 73/73, QRA stress 548/548, host 14/14, zero warnings. Awaiting test on the 10.13 Mac.

## Issue (2026-11-11): v1.0/v1.1 crashed in REAPER on macOS 10.13 (VST3 and AU)
- Not reproducible on macOS 15: REAPER 7.83 loads both formats, opens windows, renders, no crash (CI job `reaper`).
- Ruled out: missing system symbols (all 590 imports exist in the 10.13 SDK), AVX/SSE4.2 code (SSE4.1 only),
  Metal path (off by default), HarfBuzz/CoreText version branches (compile-time, classic path on 10.13).
- Waiting on the macOS crash report from the High Sierra machine.

## v1.1 (2026-11-10)
Researched how Gullfoss' Recover/Tame behave (manual + reviews) and closed the gaps.
- Recover/Tame 0–200% (old values load unchanged; 30–50% is the gentle zone).
- Reference switch: **Ear (ISO 226)** or **Pink Noise** (long-term balance, power-averaged, analytically calibrated,
  doesn't fill intentionally empty regions like a high-passed sub).
- Bias now moves the Recover/Tame split (matters even with only Recover). Brighten does nothing alone, tilts the
  split by frequency, and drives a Darker/Brighter meter. Recover/Tame activity meter added.
- Boost = listening level on the ISO 226 contours (±50 dB, global); old output gain moved to new **Output**.
- Loudness match on by default, predictive and K-weighted (within 0.4 dB in tests).
- Optional 20 ms lookahead, latency reported to the host; bypass and Amount 0 are bit-exact with it on.
- Tests: 73/73 DSP checks (ASan/UBSan clean), host test 14/14, editor stress 5/5, zero warnings.
- Bugs the new tests caught: dB-averaging bias (pink read 3–5 dB low in the lows), lookahead bypass off by float
  rounding, loudness prediction overshoot (1.3–1.7 dB), Brighten too weak (0.09 Bark range, now −0.20…+0.52 Bark).
- CPU unchanged: ~0.6% of one core at 48 kHz with everything on.

## v1.0 (2026-11-10)
Built from the SpectralUnmasker header: kept its structure, fixed its bugs, added the missing pieces, renamed it WaveSurfer.

### Fixes to the original code
- Kalman: Q is now solved from a time constant, so the time constant is identical at every rate.
  The old 0.16 ms (44.1k) vs 0.11 ms (96k) became a frequency-dependent 15–250 ms, run at control rate on dB levels.
- SmoothedValues: snap to the live value on the first block (no fade-in from 0).
- Low > High limits are swapped instead of producing nonsense.
- `fs` is set in prepare. Parameter pointers are cached.
- NaN/inf input → 0, input clamp at +60 dBFS, denormal input → 0, filter-state health check with auto-reset.
- ScopedNoDenormals in processBlock.
- Clipper and fastTanh kept unchanged (verified correct: C1 knee, monotonic, ceiling 1.0).

### Added
- Gains-to-audio stage: 16-bell TPT cascade with overlap compensation; coefficients updated at control rate.
- ISO 226 (70 phon) weighting, Terhardt threshold gating (no noise-floor lifting), two-pass Bark spreading.
- Gain match, Delta, click-free Bypass (host-linked), Safety Clip toggle, mono support, 0.1 s tail.
- Lock-free GUI snapshot (atomics); meters accumulate peaks until the GUI reads them.
- UI: 1060x664 canvas, scalable with a locked aspect ratio, embedded Inter font.

### QC results (Linux x86-64 cloud VM, JUCE 9.0 @ d60b7a7)
- Stress test: 42/42 pass. Sanitizer (ASan+UBSan) clean.
- Bugs found and fixed by the tests: 1e30 hit rang for seconds (14 dB off after 2 s);
  Tame too weak on a narrow resonance; denormal input 39x slower.
- Host test on the built VST3: 12/12 pass (state error 3e-8, mono OK, editor lifecycle OK).
- Editor stress: 5/5 real windows under audio + automation load.
- CPU (stereo, block 512, all bells active, single core): 0.61% @44.1k, 0.63% @48k, 1.04% @96k, 1.87% @192k.
  Worst block ≤ 1.04x average (no spikes).
- Bit-exact null at default settings and at Amount 0. Output is bit-identical for block sizes 1…8192.

### macOS CI (GitHub Actions, macos-15, 2026-11-10)
- Universal (arm64 + x86_64) VST3/AU/Standalone built against JUCE d60b7a7, x86_64 slice min macOS 10.13.
- Same stress test 42/42, VST3 host test 12/12, `auval -strict`: AU VALIDATION SUCCEEDED.
- Release v1.0.0 published by CI: WaveSurfer-1.0.0-macOS.pkg + .zip (ad-hoc signed, not notarised).

### Not yet verified
- Runtime on a real High Sierra machine and in DAWs (Logic, Ableton, Reaper...).
- Real-mix listening and tuning of the gain-law constants (RECOVER_SLOPE, TAME_SLOPE, TAME_DEADZONE).
- Side-only (L = −R) material: detection uses Mid, so pure side content is left untouched (by design, for now).
