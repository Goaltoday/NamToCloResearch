# v2.6.0 Exact VST Tone Match Replication

This branch is a diagnostic comparison against the manually successful Corrective IR workflow.

- SOURCE: official CLO render, final 20 s.
- TARGET: HTUSBTools NAM render, final 20 s.
- Analysis: SOURCE_latest_19 CAB Tone Match style (FFT 16384, Hann, 75% overlap, 11 robust groups, RAW TARGET-SOURCE).
- Smooth: 5%.
- Solver: minimum-phase IR, 2048 samples.
- The generated IR is saved as `<NAM>_auto_tonematch_ir.wav`.
- Crucially, the generated WAV is then applied with the existing `applyCorrectiveIrToClo()` function, exactly like a manually selected corrective IR.
- PRE, A, P/K and POST are untouched.

Compare the exported auto IR directly with a manual Tone Match IR such as `modelo4.wav`, and compare Original / VST_EXACT_BEST / manual-Corrective-IR CLO in Plugin Doctor.
