# P/K refinement v1.9.8 - nonlinearity-specific loss

Goal: avoid candidates that match average spectrum/envelope while sounding more distorted than the NAM.

- Full render remains in use (normally 50 s stimulus + 20 s tail).
- Output scale is fitted once from the original CLO and frozen.
- Input stimulus is divided into 2048-sample non-overlapping windows. Silent windows below -60 dB relative to the loudest input window are ignored.
- Remaining windows are split by RMS tertiles into LOW / MID / HIGH excitation.
- Candidate loss: 35% global NMSE + 35% mean normalized LOW/MID/HIGH NMSE + 15% MR-STFT + 15% multi-scale envelope.
- _BEST is exported for audition.
- _REFINE requires no global NMSE regression, max 0.5% regression in any excitation group, and secondary MR-STFT/envelope guards.

First validation case: Fender Clean that produced excessive distortion in v1.9.7 BEST. Compare NAM / original CLO / BEST / REFINE at matched output level.
