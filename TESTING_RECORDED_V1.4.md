# NAM to CLO v1.4 - Recorded Audio test

## Purpose

Validate replacing the 20-second `PresetAudio.wav` tail with a user recording while leaving the already validated worker, `namConvertCloData`, Ampero 2048 output and GP-200 1024 compaction unchanged.

## Confirmed behavior preserved

- `Original / Legacy` still copies `nam_input_wav.wav` byte-for-byte.
- Clean/Dist still use the original 50-second Hotone source WAV.
- Sound Clone-style stimuli still append 600 zero samples.
- The generated stimulus handed to `namConvertCloData` is mono PCM16 at 44.1 kHz.

## Recorded Audio behavior in v1.4

The selected user WAV can be shorter, equal to, or longer than 20.000 seconds. The program automatically converts supported WAV audio to mono PCM16 44.1 kHz and then forces the tail to exactly 20.000 seconds: longer recordings are trimmed from 20 s onward and shorter recordings are padded with digital silence at the end.

Supported sample encodings:

- PCM 8-bit
- PCM 16-bit
- PCM 24-bit
- PCM 32-bit
- IEEE float 32-bit
- IEEE float 64-bit
- classic WAV and WAVE_FORMAT_EXTENSIBLE when its subformat is PCM or IEEE float

Automatic adaptation:

- multichannel -> mono by averaging channels
- source sample rate -> 44.1 kHz using linear interpolation
- source sample format -> PCM16

The program does **not** normalize, amplify, attenuate, limit, fade or otherwise alter the level intentionally.

## Suggested regression test

1. Convert the same NAM with v1.3 and v1.4 using `Original / Legacy`; outputs should remain byte-identical.
2. Convert using `Clean + Original Preset Audio`; compare with the v1.3 Clean output.
3. Convert using `Dist + Original Preset Audio`; compare with the v1.3 Dist output.
4. Select a user recording and test `Clean + Recorded Audio` and `Dist + Recorded Audio`.
5. Test one file longer than 20 s and verify only the first 20 s are used.
6. Test one file shorter than 20 s and verify silence is appended to reach 20 s.
7. Repeat with stereo 48 kHz or 24-bit WAV to exercise automatic adaptation.

## Intentionally out of scope

- automatic trimming or padding of user recordings
- level normalization
- CAB No / native 512 identification
