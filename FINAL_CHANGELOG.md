# v2.7.0-FINAL changelog

This is the completed independent NAM -> CLO reconstruction pass.

## Final closure items

- Official trainer input/target buffers are exactly `float(Fs) * 70 + 600` samples.
- The original stimulus is resampled as exactly 70 seconds; the 600 guard samples are appended **after** SRC and are not passed through NAM inference.
- NAM target is rendered for exactly the 70-second signal, scaled by `0.31f`, then receives the same 600 zero guard.
- Target preprocessing matches the reconstructed float32 two-pass path: mean removal followed by linear detrend using `x = 1..N`.
- Latency detection searches exactly 600 samples from `6 * int(float(Fs))`, threshold `fabs(x) > 0.01`, and returns 600 on no hit.
- Historical r8brain compatibility is pinned to `version-3.7`, the latest pre-v4 template-era release. The official EXE RTTI identifies `CDSPResampler24` / `CDSPResampler<CDSPFracInterpolator<24,673>>`; r8brain 4.0 removed templated interpolation parameters.
- The SRC wrapper uses the stable `process(double*, int, double*&)` primitive and explicitly reproduces one-shot zero flushing, avoiding revision-specific `oneshot()` signatures.
- `r8bbase.cpp` is compiled automatically when supplied by the historical r8brain tree.

## Retained reconstructed trainer path

- NAM inference in 1024-sample blocks and target scale `0.31f`.
- Ppos/Pneg extrema and K slope + 0.80..1.20 branch searches.
- Fixed 50-tap initial FIR tables for 44.1/48/96 kHz.
- 125 ms Hamming, 50% overlap, modulo-2048 folded cross-spectrum estimator.
- Magnitude-only `|Sxy|/(Sxx+FLT_EPSILON)` transfer estimate.
- Official dB/Mel/Gaussian conditioning order and float/double boundaries.
- Cepstral minimum-phase reconstruction with full length `2*N-2`.
- Sequential geometric A smoothing below ~60 Hz.
- Partitioned FIR processing: 64-sample hop / FFT128.
- A/B phase schedule 3 + 2 + 5, float32 factor states, frequency weighting, step decay, rollback, and 512-point Mel loss over 80..10000 Hz.
- Final 50..70 s B refinement, 256-tap minimum-phase corrector, convolution, mean removal, and energy normalization.
- POST coefficients computed as float32 then promoted to CLO doubles; trainer recurrence retains the x1000 / float-round / x0.001 path.
- Final A/B SRC to 44.1 kHz, B x4 serialization, B2048 intermediate and GP-200 B1024 compact output.

The legacy HTUSBTools tab and the separate Stimulus Profile / Tail UI workflow are unchanged.
