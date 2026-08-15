# v2.5 VST CAB Tone Match — final 20 seconds

This branch replaces the v2.4 custom spectral matcher with a direct standalone port of the Tone Match algorithm in `SOURCE_latest_19`.

## What is copied from the VST algorithm

- ToneAnalysis: FFT order 14 (16384), Hann, 75% overlap, silence rejection below -55 dBFS, clipping rejection at 0.999, 30 Hz–20 kHz.
- Robust spectrum: 11 interleaved groups, median-of-means in dB and MAD-based stability confidence.
- ToneMatchComparison: 512 logarithmic points from 40 Hz to 18 kHz, canonical RAW correction = TARGET - SOURCE, no automatic level offset removal.
- SolverV1: minimum-phase 2048-sample IR (v2.5.2 extension; analysis and smoothing unchanged).
- Smooth is fixed at 5%, corresponding through the VST slider mapping to 1/120-octave CAB smoothing.

## Converter-specific integration

- Only the FINAL 20 seconds are analysed.
- SOURCE = official CLO render through the reconstructed CloPlayer.
- TARGET = HTUSBTools NAM render.
- The converter's one-time frozen output scale is applied to SOURCE before capture so arbitrary CLO wrapper output level does not become an EQ correction.
- PRE, A, P/K and POST never change.
- The generated 2048-sample minimum-phase Tone Match IR is convolved into Ampero Block B (2048 taps), truncated to 2048 taps, and only then compacted to GP-200 B1024 by the existing converter.

## Files

- `_B_TAIL_TONEMATCH_BEST.clo`: always contains the direct VST-style tone-match candidate for audition.
- `_B_TAIL_TONEMATCH_REFINE.clo`: contains the candidate only when the final-20-second VST-style tone error improves and broad NMSE/MR-STFT guards pass; otherwise it falls back to the official CLO.

The main diagnostic is `VST final-20-s tone-match error`, not the older v2.4 direct-spectrum MAE.


## v2.5.2 change
Only the SolverV1 IR length changes from 1024 to 2048 samples. The cepstral synthesis FFT is increased from 2048 to 4096 so the minimum-phase construction has the corresponding support. All tail-selection, analysis, comparison and smoothing settings remain identical to v2.5.0.
