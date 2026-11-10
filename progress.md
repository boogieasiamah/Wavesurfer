# WaveSurfer — progress

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

### Not yet verified
- macOS AU/VST3 builds and High Sierra runtime (built and tested on Linux only).
- Real-mix listening and tuning of the gain-law constants (RECOVER_SLOPE, TAME_SLOPE, TAME_DEADZONE).
- Side-only (L = −R) material: detection uses Mid, so pure side content is left untouched (by design, for now).
