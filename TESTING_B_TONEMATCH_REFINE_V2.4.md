# v2.4 Automatic Corrective-IR Tone Match

v2.4 keeps PRE, Block A, P/K and POST fixed and changes only the Ampero 2048-tap Block B.

For every conversion the program already has the exact stimulus, the HTUSBTools NAM render and the official CLO. v2.4 renders the official CLO with the same stimulus, compares the final NAM and CLO output spectra directly, removes a global level offset, smooths the dB residual and turns that residual into an automatic corrective-IR style filter which is absorbed into Block B.

The matcher performs up to three conservative iterations. Maximum correction is reduced from +/-4 dB to +/-2 dB and finally +/-1 dB. At each iteration 35%, 55%, 75% and 100% of the measured correction are tested; only a candidate that actually reduces direct output spectral-shape error is allowed to become the next iteration.

The search target is spectral closeness. Full-render NMSE and MR-STFT are broad safety guards rather than the optimisation target, because Block B is a post-nonlinearity linear filter and cannot repair nonlinear waveform differences.

Outputs:
- `_Ampero_2048_B_TONEMATCH_BEST.clo`: best spectral candidate found, always exported.
- `_Ampero_2048_B_TONEMATCH_REFINE.clo`: candidate accepted by the final safety gate, otherwise the official CLO.
- corresponding GP-200 1024 compact files.

Recommended test: compare the original and `_B_TONEMATCH_BEST` against the NAM with the same external analyser previously used for the Fender clean model.
