# WaveSurfer — progress

## v1.2.0 (2026-11-13): no more low-mid build-up, Ref Track matching, GUI CPU fixed
Measured with Tests/LowEndProbe (noise shapes, synthetic song, vocal over room boom), Tests/SpikeProbe and
`WaveSurferSnapshot --paint-bench`.
- **Muddy low-mid shift fixed.** Loudness make-up was K-weighted (BS.1770 counts 50 Hz almost like 1 kHz): when Tame
  cut the kick, the make-up lifted 150-400 Hz by 1.2 dB. Make-up now follows equal loudness at 80 phon:
  150-400 Hz +0.2 dB (new regression test T18 fails the old engine at +1.23 dB). LUFS may read up to ~1 dB lower
  when a lot of sub is cut; perceived loudness holds within 0.1 dB (T11, independent 1/6-octave meter).
- **Bells now deliver the curve.** The 16 bells were fitted by pairing + one overlap factor and delivered only ~half
  the gain above 5 kHz and in the lows. Least-squares fit (precomputed 16x32 matrix): error vs requested curve
  roughly halved (tilt 2.5 -> 1.2 dB, shelves 5.2 -> 2.6 dB at band centres). Same CPU.
- Adaptive gains below 400 Hz move no faster than ~60 cycles of the band (follow the bass line, not each kick):
  low-end gain swing on a kick+bass song 5.0 -> 2.1 dB p-p. Amount/Boost/limits keep their 40 ms response.
- **Ref Track mode** (Ear | Pink | Ref): load a song (WAV/AIFF/FLAC/MP3/Ogg; M4A on Mac); a separate engine analyses its
  long-term band balance on a worker thread with the same filter bank and pink calibration; Track mode steers toward
  it (slope 1.0, 0.5 dB deadzone). Saved in the session (name + curve), re-mapped on sample-rate change. Balance error
  vs a reference 5.1 -> 1.8 dB at Recover/Tame 100% (T19). Right-click LOAD REF to clear.
- Output safety clamp at +24 dBFS (QRA rule 1.5) - exposed once the lows were delivered in full (+24.2 dBFS case).
- **GUI CPU spike fixed.** The display redrew gradients, glow strokes on 240 points and the window background 30x/s:
  6.5 ms/frame (20 % of a core) at 1x, 25 ms (77 %) on Retina. Now cached static layers, buffered background,
  cached knob bodies (the blurred shadow), cheaper glow, and no repaint when nothing moved: 0.56 ms / 2.3 ms
  (2 % / 7 %); full-window redraw 57 -> 5 ms. Pixel diff vs before: 0.07 % of pixels, max 42/255 at glow edges.
  The audio engine itself is flat: worst block <= 1.6x median at fixed block sizes.
- Tests: DSP 81/81 (ASan/UBSan clean), QRA stress 548/548, host 14/14, Ref Track round trip (in-process), zero warnings.
- Comparison pack + Tests/compare_renders.py ready for Gullfoss / Smooth Operator renders (none published).

## v1.1.1 (2026-11-11): rebuilt on the Flux 512 setup — macOS 10.13+ and Windows
- JUCE 9 master -> **JUCE 8.0.10** (FetchContent), the release Flux 512 runs on the same High Sierra Mac.
- New Windows build: VST3 + Standalone x64, static MSVC runtime, Inno Setup installer.
- Added the QRA generic extreme-parameter test from Flux 512 (548/548 pass locally; heaviest case 4% of real time).
- CI now also checks every required import of the Intel binary against the macOS 10.13 SDK.
  First run flagged kIOMainPortDefault and os_workgroup_join/leave/max_parallel_threads (macOS 11/12 only).
  They are dead symbol-table entries left after LTO: no bind or lazy-bind fixup refers to them, so dyld never
  resolves them. Flux 512 v0.2.3 (works on 10.13) has the same four. The check now counts only bound symbols.
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
