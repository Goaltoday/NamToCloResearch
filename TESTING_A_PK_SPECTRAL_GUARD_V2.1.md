# v2.1 A+P/K spectral-response guard test

Use the same NAM and settings as the v2.0 comparison (Legacy stimulus, Original Preset Audio tail, Corrective IR off).

Compare three models against the NAM with the same external spectral analyser:

1. Original GP200_1024 CLO.
2. v2.1 `_A_PK_BEST` GP200_1024 CLO.
3. v2.1 `_A_PK_REFINE` if accepted.

The completion dialog now reports `Transfer-profile dB-shape MAE (original -> BEST)` and its percentage improvement. `_BEST` is no longer the unconstrained minimum of the research loss: it is the best searched candidate that keeps the input-referenced 30 Hz-20 kHz spectral-profile error within +0.10% of the original.

The spectral profile is derived from the known first 50 seconds of stimulus using 4096-sample Hann/Welch spectra, 2048-sample hop and 96 equal-log-frequency bands. A global output-level offset is removed so the metric measures contour rather than volume.
