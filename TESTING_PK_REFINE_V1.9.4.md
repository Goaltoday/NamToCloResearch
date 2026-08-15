# P/K refinement v1.9.4 — Research Loss

This version evaluates the complete render (normally 50 s stimulus + 20 s tail).

Metrics:
- raw full-waveform NMSE (fixed output calibration from original CLO),
- multi-resolution STFT: FFT 512/2048/8192, Hann windows, 50% hop, spectral convergence + log-magnitude,
- multi-scale RMS envelope: 256/2048/8192 sample windows, mean absolute dB error.

Acceptance:
- candidate raw NMSE may not worsen versus the original CLO,
- candidate MR-STFT may not worsen versus the original CLO,
- envelope may not worsen by more than 0.05%,
- weighted normalized composite (35% NMSE, 50% MR-STFT, 15% envelope) must improve versus current best.

Only Ppos/Pneg/Kpos/Kneg are modified. PRE, POST, A and B remain fixed.

Why no A-weighted ESR yet:
The reviewed guitar-amplifier literature supports perceptual pre-emphasis, including A-weighting. We deliberately defer it in this revision because A-weighting strongly de-emphasises low bass, while our current failure case showed a clearly audible/visible low-mid mismatch. First validate the more neutral raw-time + MR-STFT + dynamics objective.
