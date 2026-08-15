# NAM to CLO v2.0.0 - A + P/K experimental refiner

This is a new branch. The v1.9 P/K-only strategy is retired as the primary refinement method.

## Conversion path
The official conversion remains unchanged and is always written first:

NAM -> HTUSBTools -> Ampero B2048 -> GP200 B1024

When the v2.0 refinement checkbox is enabled, an additional branch is evaluated from the raw Ampero B2048 model:

Ampero B2048 -> refine Block A + P/K -> A_PK_REFINE Ampero -> compact -> A_PK_REFINE GP200

PRE, POST and B remain fixed in v2.0.0.

## Block A parameterisation
Block A remains exactly 128 taps in the exported CLO. During optimisation it is represented by a smooth 10-point magnitude envelope at:
40, 80, 160, 315, 630, 1250, 2500, 5000, 10000 and 18000 Hz.

The envelope modifies the magnitude of the original A response while preserving its phase, then a 128-tap causal FIR is reconstructed. Each point is limited to +/-4 dB from the official A.

## Search
Every candidate is evaluated on the complete render (normally 50 s stimulus + 20 s tail). The output scale is fitted once to the official CLO and then frozen.

Each pass alternates:
1. coordinate refinement of the 10 Block-A bands;
2. coordinate refinement of Ppos/Pneg/Kpos/Kneg.

Initial steps: 0.75 dB for A and about 5.7% multiplicative for P/K. Steps shrink after every pass.

## Metrics
Combined research loss:
- 45% full-render NMSE
- 25% low/mid/high excitation-balanced NMSE
- 20% multi-resolution STFT (512/2048/8192)
- 10% multi-scale RMS envelope (256/2048/8192)

Final REFINE safety gate additionally rejects material temporal/spectral/dynamic regressions. BEST is always exported for audition.

## First validation
Use the same reference cases used for v1.9 (Fender Clean and 5150), Legacy stimulus, Original Preset Audio tail, Corrective IR OFF. Compare NAM / official GP200 / A_PK_BEST / A_PK_REFINE at matched listening level.
