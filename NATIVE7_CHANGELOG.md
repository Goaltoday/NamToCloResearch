# NATIVE7 trainer corrections

Applied from the full reverse-engineering conclusions before golden validation:

- Restored official P/K seed + 0.80..1.20 multiplier search.
- Restored low-level/conditioned-sweep initialization of spectral A/B factor states.
- Restored exact 1/5-point initial sweep Gaussian passes and reference-derived soft floor.
- Added reconstructed frequency weighting (1.0 -> 0.5 from ~80 Hz to Nyquist) in the correction exponent.
- Reset step/best/correction separately for the 3, 2 and 5 iteration phases and restore each phase's best solution on exit.
- Replaced fixed smoothing approximations with dynamic 0x554f00 Mel/dB conditioning kernel lengths.
- Preserved even-length Gaussian kernel behavior and sequential in-place A smoothing below ~60 Hz.
- Replaced fixed 2048-point minimum-phase construction with the official 2*N-2 sizes: 254 for A, 2048 for B, 510 for the final 256-tap corrector.
- Replaced linear whole-band loss sampling with the exact 512-point uniform-Mel 80 Hz..10 kHz grid.
- Corrected final 50-70 s folding to SUM rather than average and restored positive-DFT -> Gaussian -> 256-point conditioner -> 510-point minimum-phase flow.
- Phase and tail model renders are performed on the selected segment with fresh DSP state.

The legacy HTUSBTools tab and the existing Stimulus Profile / Tail architecture are unchanged.
