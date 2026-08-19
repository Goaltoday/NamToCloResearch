# 2.7.0-FINAL5-EXACT

This revision replaces remaining numerical shortcuts in the independent trainer with paths reconstructed from GP-200.exe.

Confirmed changes implemented:

- PRE biquad is executed in trainer renders, including the initial no-FIR measurements.
- Trainer biquad preserves the executable's float/double rounding boundaries.
- FIR A/B engine uses 64-sample partitions, 128-point transform, 65 stored real-spectrum bins and float32 partition-major accumulation.
- FIR transform uses the Q15 trigonometric table recovered from GP-200.exe rather than runtime sin/cos twiddles.
- 2048-point spectral estimator uses Takuya Ooura's double-precision FFT4G RDFT path.
- Minimum-phase reconstruction uses the executable's direct O(N^2) float DFT path and recovered cepstral lifter behavior.
- Direct-DFT trigonometric tables generate only the first half with libm and fill the second half by IEEE-754 sign-bit inversion.
- Hamming windows preserve the executable's float phase/cosine boundary and double multiply/subtract before final float rounding.
- Final 50-70 s Block-B refinement computes complete length-L DFTs for target and model before extracting positive-frequency magnitudes.
- Existing confirmed P/K, conditionMagnitude, 3/2/5 optimizer schedule, rollback, B x4, FIR SRC, 70 s + 600 zero guard, detrend/latency alignment and GP-200 B1024 compaction are retained.

No empirical A/B gain compensation or manual sample shift is added.
