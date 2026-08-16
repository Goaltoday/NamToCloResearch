# NAM to CLO v2.6.7

## v2.6.7 — matched-input refinement

This version is based directly on v2.6.6.1. The abandoned v2.6.7 branch that compared an unrelated external target WAV directly is not included.

The optional refinement test WAV is now used as an **input performance**, not as a pre-rendered Tone Match target:

1. The first 20 seconds of the selected WAV are automatically adapted to mono PCM16 44.1 kHz using the existing Recorded Audio adapter.
2. Those 20 seconds replace the tail of a second otherwise-identical 70-second conversion stimulus.
3. HTUSBTools renders that exact stimulus through the NAM. A2 SlimmableContainer models still use the verified Full submodel (highest `max_value`).
4. The already-created original Ampero B2048 CLO renders the exact same second stimulus in the offline CLO player.
5. Tone Match compares the common final 20 seconds: NAM render (TARGET) versus original CLO render (SOURCE).
6. The correction is applied only to Block B and the refined result is compacted to GP-200 B1024.

Leaving the refinement test WAV blank keeps the v2.6.6.1 behavior: the original conversion stimulus/NAM render pair is used.

The selected refinement WAV may use the WAV formats already supported by the Recorded Audio adapter (PCM 8/16/24/32-bit or IEEE float 32/64-bit, mono or multichannel, arbitrary sample rate). It is downmixed/resampled automatically and the **first** 20 seconds are used; shorter files are zero-padded according to the existing adapter behavior.

Comparison diagnostics are not shown. With refinement enabled, the output folder receives only:

1. `<name>_Ampero_2048.clo`
2. `<name>_GP200_1024.clo`
3. `<name>_GP200_1024_REFINED.clo`

Intermediate candidate CLOs, the second-pass CLO generated only as a side effect of HTUSBTools, and `auto_tonematch_ir.wav` remain temporary and are deleted with the work directory.

## NAM A2: always use Full

For an A2 `SlimmableContainer`, the converter selects the embedded model with the highest `max_value` and supplies that standard NAM model to HTUSBTools. This was verified with `Modelo4.nam`: the 8-channel `max_value=1.0` model matches A2 Full, while the 3-channel `max_value=0.5` model is Lite.

## Corrective IR and refinement level

Manual Corrective IR keeps the historical -6 dB post-correction gain. Automatic refinement uses 0 dB post-correction gain after Block-B RMS matching.

## Tone Match implementation

The current refiner remains VST-style rather than an exact export replica of `SOURCE_latest_19`: it uses the existing 2048-sample minimum-phase correction path rather than the VST's exact 1024-sample SolverV1/crest-optimised export pipeline.
