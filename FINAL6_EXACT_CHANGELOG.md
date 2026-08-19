# v2.7.0-FINAL6-EXACT

This pass removes remaining reconstructed-math shortcuts found by tracing the
native GP-200.exe trainer helpers directly.

Confirmed EXE-derived changes:

- `0x4225b0` float linspace is now incremental (`out[i]=out[i-1]+delta`) and
  forcibly overwrites the final endpoint. It is used for the FFT frequency
  grid, Mel grids, the 1.0->0.5 optimizer weight ramp, and the loss grid.
- `0x553c60` interpolation is reproduced literally: float slope/intercept
  precomputation, full source scan for the closest non-negative query-source
  distance, and `slope*q+intercept` float operation order.
- `0x555460` Gaussian smoothing is reproduced with its 1,000,000 kernel
  normalization, reversed double kernel, N+1 zero slot for even kernel sizes,
  1e-6 interior output scaling, and edge-only weight renormalization.
- `0x554f00` conditionMagnitude now follows those exact helpers, removes the
  non-official input magnitude clamp, and preserves the source/destination
  Mel-Hz endpoint overwrites performed by the EXE.
- `0x4223c0` was resolved through the import table as CRT `floor`; the 60 Hz
  sequential A smoothing and optimizer weight boundary use that operation.
- The 512-point loss-frequency grid is now built exactly as the EXE
  constructor does: incremental Mel linspace, interior Mel->Hz conversion,
  exact 80 Hz / 10 kHz endpoints, followed by `0x553c60` interpolation.
- Initial sweep regularization now follows the float/double operation order of
  `0x5585a5..0x5586xx` rather than an algebraically simplified expression.
- No empirical A gain, B gain, or sample shift has been added.

The existing HTUSBTools conversion path and Stimulus/Tail UI architecture are
unchanged.
