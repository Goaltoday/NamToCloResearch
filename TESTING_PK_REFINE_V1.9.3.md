# P/K refinement v1.9.3 — multi-metric validation

Use the same conversion settings as the baseline tests (for example Legacy + Original Preset Audio + Corrective IR OFF).

The complete render is evaluated. Output scale is calibrated once from the original CLO and frozen. A P/K candidate is accepted only if temporal NMSE, spectral error and RMS-envelope error all do not worsen.

The completion dialog reports:
- full-render NMSE improvement,
- first-50-s NMSE improvement,
- tail NMSE improvement,
- 48-log-band spectral improvement,
- RMS-envelope improvement.

For a trustworthy refinement all reported guarded metrics should be >= 0. If no safe improvement is found, the REFINE CLO keeps the original P/K values.
