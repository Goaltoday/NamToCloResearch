# v2.3 Automatic Wiener Block-B Match

## Purpose

This branch replaces the v2.2 heuristic dB-band iteration with a direct, per-conversion linear identification step.

The program already has all required signals for each NAM conversion:

1. the exact conversion stimulus,
2. HTUSBTools' rendered NAM target (`outputFile.wav`), and
3. the official Ampero CLO generated from the same conversion.

The official CLO is rendered with the reconstructed CloPlayer core. A regularized complex Wiener correction is estimated from that render to the NAM target and absorbed only into the Ampero 2048-tap Block B. PRE, Block A, P/K and POST are never changed.

## Algorithm

- Render official CLO with the exact conversion stimulus.
- Fit one global output scale from the original CLO and freeze it.
- Estimate the complex correction with Welch cross/auto spectra:
  `C(f) = S_yx(f) / (S_xx(f) + lambda)`.
- Use FFT 8192 / hop 4096 / Hann analysis.
- Test regularization fractions `1e-5, 1e-4, 1e-3, 1e-2`.
- Clamp individual correction magnitude to +/-6 dB and phase to +/-45 degrees.
- Shrink unreliable bins toward identity according to input power / regularization.
- Test correction blend factors `0.20, 0.35, 0.50, 0.70, 1.00`.
- Multiply the original Block-B spectrum by the correction and reconstruct the same 2048-tap causal Block B.
- Choose the candidate with the lowest full-render NMSE that does not worsen the direct output spectral-shape MAE by more than 0.10%.
- Final acceptance additionally requires at least 0.10% NMSE improvement and no more than 1% MR-STFT regression.
- Only after matching is the existing Ampero B2048 -> GP-200 B1024 compaction performed.

## Expected output files

- `_Ampero_2048_B_WIENER_BEST.clo`
- `_GP200_1024_B_WIENER_BEST.clo`
- `_Ampero_2048_B_WIENER_REFINE.clo`
- `_GP200_1024_B_WIENER_REFINE.clo`

`BEST` is the best spectrally-safe Wiener candidate found. `REFINE` is the accepted candidate; if the gate rejects the match, REFINE falls back to the official CLO.

## Recommended first test

Use the same Fender clean NAM used for v2.2, with Legacy stimulus + Original Preset Audio tail + Corrective IR OFF. Compare the original and `_B_WIENER_BEST` in the same external analyzer before listening. The v2.3 result should not be accepted if its broad spectral contour is visibly worse even when other metrics improve.
