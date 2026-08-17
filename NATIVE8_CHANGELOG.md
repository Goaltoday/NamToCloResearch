# v2.7.0-NATIVE8

Strict-equivalence pass after comparing NATIVE7 with the official GP-200 conversion of the same 48 kHz WaveNet NAM.

## Corrections transcribed from GP-200.exe

- POST biquad is calculated in float32 and then promoted to the CLO double fields.
- 50-tap initial conditioning FIR accumulates in float32.
- Main 125 ms spectral estimator now keeps frame means, folding and Sxx/Sxy accumulation in float32.
- `conditionMagnitude` (0x554f00) now uses float32 working arrays/constants for Mel, dB, Gaussian smoothing and interpolation.
- Gaussian helper (0x555460) retains the even-length N+1/zero-slot behavior with float32 normalization and edge renormalization.
- Minimum-phase reconstruction uses float32 working buffers, float32 norm/mean/scaling, and the confirmed full length `2*N-2`.
- Final Block-B tail stage now conditions target magnitude and candidate-model magnitude independently **before** computing their ratio, matching calls at 0x556e2a and 0x556e44.
- Final tail ratio uses the official double `1e6` / `FLT_EPSILON` division and stores the result as float32 before clamping.
- Final B mean removal and energy normalization use float32 accumulators/sqrt/division.
- 48/96 kHz FIR resampling uses the truncated effective output coefficient count before zero padding. For A128 at 48 kHz this is 117 samples, matching the official golden CLO rather than NATIVE7's 118.

The legacy HTUSBTools path and the existing independent Stimulus Profile / Tail architecture are unchanged.
