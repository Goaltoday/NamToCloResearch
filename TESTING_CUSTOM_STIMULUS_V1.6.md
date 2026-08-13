# NAM to CLO v1.6 - Custom Stimulus test plan

v1.6 adds a fourth Stimulus profile: **Custom WAV**.

The selected Custom Stimulus WAV is adapted before conversion to:

- mono
- PCM16
- 44.1 kHz
- exactly 50.000 seconds

Files longer than 50 seconds are trimmed to the first 50 seconds. Shorter files are padded with digital silence at the end. Supported input encodings are PCM 8/16/24/32-bit and IEEE float 32/64-bit. Multichannel audio is mixed to mono. No level normalization or gain processing is applied.

The resulting 50-second custom stimulus then uses the existing Tail / Reamp source exactly like Clean and Dist:

- Original Preset Audio -> 20 s PresetAudio
- Recorded Audio -> user WAV adapted to exactly 20 s

The existing 600 zero samples are appended after the 20-second tail, as in v1.5.2.

## Regression tests

1. Original / Legacy must remain byte-identical to the validated v1.5.2/v1.1 outputs.
2. Clean + Original Preset Audio must remain byte-identical to v1.5.2.
3. Dist + Original Preset Audio must remain byte-identical to v1.5.2.

## Custom Stimulus equivalence tests

These are the most useful first tests because they validate the new adaptation path without changing the actual stimulus content:

1. Select **Custom WAV** and choose the official `inputSignalCleanSW.wav`.
2. Use **Original Preset Audio** as the tail.
3. Convert the same NAM previously tested in Clean mode.
4. The generated CLO should be byte-identical to **Clean + Original Preset Audio**.

Repeat using official `inputSignalDistSW.wav`. The result should be byte-identical to **Dist + Original Preset Audio**.

## Adaptation tests

After the equivalence tests pass, try copies of the same custom stimulus exported as:

- stereo 48 kHz / PCM24
- mono 96 kHz / float32
- shorter than 50 s (expect zero-padding)
- longer than 50 s (expect trimming)

These tests exercise the automatic adaptation path.
