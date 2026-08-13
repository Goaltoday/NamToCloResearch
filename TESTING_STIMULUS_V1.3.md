# NAM to CLO v1.3 - Sound Clone stimulus validation

This version keeps the validated conversion core unchanged and finalizes the stimulus profiles discovered during v1.2 testing.

## Profiles

- `Original / Legacy`: byte-for-byte copy of `nam_input_wav.wav`.
- `Clean`: `inputSignalCleanSW.wav` (50 s) + `PresetAudio.wav` (20 s) + 600 zero samples.
- `Dist`: `inputSignalDistSW.wav` (50 s) + `PresetAudio.wav` (20 s) + 600 zero samples.

Clean and Dist are written as mono PCM16 / 44.1 kHz. v1.2 testing showed that mono and dual-mono variants produced identical CLO files, so the redundant stereo test modes were removed.

## Regression test

1. Convert the same NAM with v1.1 or v1.2 Legacy and keep both CLO files as references.
2. Convert with v1.3 `Original / Legacy`.
3. Confirm both Ampero 2048 and GP-200 1024 outputs are byte-for-byte identical to the reference.
4. Convert the same NAM using `Clean` and `Dist`.
5. Compare the resulting CLOs with the previous v1.2 Clean/Dist outputs to determine whether the official 600-sample trailing silence affects `namConvertCloData`.

CAB/512 and Recorded Audio remain intentionally out of scope for v1.3.
