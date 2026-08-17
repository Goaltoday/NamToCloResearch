# Independent NAM -> CLO trainer: NATIVE5 baseline

NATIVE5 applies the complete reconstructed conversion flow currently established in the research chat instead of the simplified NATIVE4 approximations.

## Implemented reconstructed flow

- NAM black-box render from the 70 s stimulus.
- NAM expected sample rate; 48 kHz fallback.
- r8b::CDSPResampler24 for stimulus/model-rate conversion and final FIR conversion to 44.1 kHz.
- NAM render in 1024-sample blocks and target scale 0.31.
- Target detrend and impulse-based latency alignment.
- Ppos/Pneg from 0-5 s extrema; independent exponential K fit against all 100 ms ramp measurements.
- PRE identity and reconstructed source-rate POST high-pass.
- A128/B2048 impulse initialization.
- Exact reconstructed 50-tap initial-conditioning table for 44.1/48/96 kHz.
- Main spectral estimator: ceil(0.125*Fs) Hamming window, 50% overlap, mean removal, folding modulo 2048, 2048 FFT, |Sxy|/(Sxx+FLT_EPSILON).
- Magnitude -> dB -> uniform Mel domain -> exact reconstructed Gaussian kernel -> linear-Hz grid.
- Geometric A smoothing below approximately 60 Hz.
- Cepstral minimum-phase reconstruction with 1e-5 magnitude floor, truncation, mean removal and norm restoration.
- A/B multiplicative factor redistribution around the fixed P/K nonlinearity.
- Main schedule: 23-28 s x3, 6-21 s x2, 30-50 s x5.
- Correction clamp [0.2,5], step 1.0, x0.9 decay, >1.2*best rollback and x0.5 step reduction.
- 512-point mean absolute log-ratio loss.
- Accumulated B spectral state in the post-nonlinearity residual solve.
- Final 50-70 s B-only refinement using ceil(0.1*Fs) periodic folding, explicit positive-frequency DFT, [0.1,10] correction, 256-tap minimum-phase correction, convolution and energy normalization.
- B x4 serialization scale.
- VTSI B2048 output plus GP-200 B1024 compact output and CRC16/MODBUS byte order used by the converter.

## Numerical identity

The algorithmic blocks above are implemented independently. Last-bit identity with the historical GP-200 Windows binary is not claimed because FFT/libm/compiler floating-point order and the exact historical r8brain revision can differ.

## Golden-reference validation

Compare each native result against the matching official/converter CLO in this order: P/K, POST, A correlation/RMSE, B correlation/RMSE, A/B magnitude response, then rendered harmonic/time-domain response.
