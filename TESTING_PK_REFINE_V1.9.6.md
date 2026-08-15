# P/K refinement v1.9.6 diagnostics

This version keeps the v1.9.5 search and final safety gate unchanged.

The difference is diagnostic visibility: the best point found by the free search is preserved in statistics before the final gate can fall back to the original CLO. The completion dialog reports:

- accepted/rejected
- combined research-loss improvement
- NMSE improvement
- stimulus and tail NMSE improvements
- MR-STFT improvement
- multi-scale RMS envelope improvement
- searched Ppos/Pneg/Kpos/Kneg
- explicit final-gate rejection reason

If the final gate rejects a candidate, the generated `_REFINE.clo` remains the original safe CLO, but the dialog still shows what the optimiser actually found.

The application title, main header, footer and completion dialog all derive their displayed version from `ntc::kVersion` and should show 1.9.6.
