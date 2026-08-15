# NAM to CLO v2.5.5 - Band-wise Residual-Guard VST Tail Tone Match

## Purpose
Prevent a globally-improving Tone Match curve from making a local frequency region worse (the observed 2-6 kHz regression on the Fender clean test).

## Algorithm
- Analyse only the final 20 seconds with the SOURCE_latest_19 CAB Tone Match analysis.
- Smooth 5%, remove global gain offset, clamp trusted correction to +/-3 dB.
- Preserve the v2.5.4 confidence/energy mask, full correction <=6 kHz, 6-8 kHz fade, and zero correction >8 kHz.
- Divide the useful range into 9 bands: 40-80, 80-150, 150-300, 300-600, 600-1000, 1-2k, 2-4k, 4-6k and 6-8k Hz.
- Start from the original Block B (all band scales = 0).
- For each band, test 25%, 50%, 75% and 100% of the proposed Tone Match correction.
- Re-render the CLO for every trial. Keep a band change only if that same band's TARGET-SOURCE residual RMSE beats the original and current result, while global tail Tone Match error does not materially regress.
- Run up to two coordinate passes.
- Convert the accepted composite curve to one 2048-sample minimum-phase IR and absorb it into Ampero Block B2048 before normal GP-200 B1024 compaction.

## Test
Use the same Fender Clean reference used for v2.5.4. Compare original vs `_B_TAIL_TONEMATCH_BEST` in the external analyzer, especially 2-6 kHz. The v2.5.5 goal is that no accepted local band correction visibly moves farther from the NAM than the original.
