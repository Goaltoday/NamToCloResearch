# v2.2 Block-B spectral refinement

This branch is intentionally separate from the v2.1 A+P/K experiment.

## What changes
- PRE fixed
- Block A fixed
- Ppos/Pneg/Kpos/Kneg fixed
- POST fixed
- **only Block B (2048 taps) is modified**

The refiner renders the fixed upstream path once, then iteratively modifies the magnitude response of B while preserving the original B phase as far as the finite FIR reconstruction permits.

## Objective
The primary objective is a direct NAM-vs-CLO **output-spectrum shape** comparison over the first 50 s stimulus:
- 4096-point Hann FFT
- 2048 hop
- 48 logarithmic bands, 30 Hz to 20 kHz
- broad level offset removed

At each iteration the residual spectrum is smoothed and limited to +/-1.5 dB per iteration. Total B correction is limited to +/-6 dB. A short line search chooses the correction amount that actually reduces the measured spectral error.

`_BEST` is always the best spectral candidate found. `_REFINE` is written only when the spectral-shape error improves by at least 0.25% and global NMSE does not regress by more than 5%.

The purpose of v2.2 is deliberately narrow: test whether the post-nonlinearity B block can move the CLO spectrum closer to the NAM without changing the nonlinearity itself.
