# NATIVE10-FINAL changelog

Final reverse-engineering closure pass based on the GP-200 v1.8 official converter.

## Newly closed

- Official 70 s + 600 trainer buffer semantics.
- 600 guard samples appended after SRC/NAM render, not before.
- Exact float32 mean removal + x=1..N linear detrend.
- Exact latency base/search/no-hit behavior.
- Explicit MSVC `/fp:precise` for the independent converter path.

## Retained from the NATIVE10 working pass

- 50-tap sample-rate-specific initial conditioning FIRs.
- 125 ms Hamming/folded-2048 cross-spectrum estimator.
- Official Mel/dB/Gaussian conditioning order and float/double boundaries.
- Cepstral minimum-phase reconstruction.
- 64-sample / FFT128 partitioned FIR processing topology.
- 3+2+5 A/B optimizer, frequency weighting, step decay, rollback and 512-point Mel loss.
- 50–70 s final B refinement with target/model conditioning before ratio, FIR256 correction and energy normalization.
- `CDSPResampler24`, B ×4 serialization, B2048 intermediate and GP-200 B1024 compact output.

No changes were made to the existing HTUSBTools conversion tab or to the user-selected Stimulus Profile / Tail workflow.
