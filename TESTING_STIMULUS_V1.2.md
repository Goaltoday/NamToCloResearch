# NAM to CLO v1.2 experimental - stimulus test

This build intentionally changes only the stimulus preparation path before the existing worker.

## Runtime files

Place beside the executable:

```text
runtime\ampero\HTUSBTools.dll
runtime\ampero\nam_input_wav.wav
runtime\ampero\inputSignalCleanSW.wav
runtime\ampero\inputSignalDistSW.wav
runtime\ampero\PresetAudio.wav
```

The proprietary runtime files are not included in this source archive.

## Test order

Use the same NAM for all tests.

1. Convert it with the original v1.1 and keep both CLO files as references.
2. Build/run v1.2 and select `Legacy (original v1.1 stimulus)`.
3. Verify the v1.2 Legacy CLOs are byte-for-byte identical to the v1.1 references.
4. Test `Clean + PresetAudio (mono test)`.
5. Test `Clean + PresetAudio (dual-mono test)`.
6. Test `Dist + PresetAudio (mono test)`.
7. Test `Dist + PresetAudio (dual-mono test)`.

The Clean/Dist test WAVs are concatenated without normalization, gain changes, resampling, fades, or other DSP. The builder requires the official source WAVs to be mono PCM16 at 44.1 kHz, with exact lengths of 50 seconds and 20 seconds respectively.

CAB/512 and Recorded Audio are intentionally not implemented in this experimental step.
